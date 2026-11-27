/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/lora_link.h"

#include <algorithm>
#include <map>
#include <random>

#include <gtest/gtest.h>

#include "support/fake_radio_bus.h"

using namespace battleship::net::lora;
using battleship::net::lora::testing::FakeBusConfig;
using battleship::net::lora::testing::FakeRadio;
using battleship::net::lora::testing::FakeRadioBus;

namespace {

constexpr std::uint64_t kStep = 5;

struct Node {
    FakeRadio& radio;
    LoRaLink link;
    Node(FakeRadioBus& bus, std::uint32_t seed, std::int16_t rssi = -60)
        : radio(bus.add_radio(rssi)), link(radio, Japan920Policy{}, seed) {}
};

// Advance the bus and all links in 5 ms steps from `from` to `to` (inclusive).
void run(FakeRadioBus& bus, std::vector<LoRaLink*> links, std::uint64_t from, std::uint64_t to) {
    for (std::uint64_t t = from; t <= to; t += kStep) {
        bus.advance_to(t);
        for (auto* l : links) l->poll(t);
    }
}

int count_type(const FakeRadioBus& bus, PacketType type, const IRadio* from = nullptr,
               std::uint64_t since_ms = 0) {
    int n = 0;
    for (const auto& r : bus.tx_log()) {
        if (from && r.radio != from) continue;
        if (r.start_ms < since_ms) continue;
        auto p = decode_packet(r.bytes.data(), r.bytes.size());
        if (p && p->type == type) ++n;
    }
    return n;
}

std::vector<std::uint8_t> payload13(std::uint8_t seed) {
    std::vector<std::uint8_t> v(13);
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<std::uint8_t>(seed + i);
    return v;
}

// Connects host and client; returns the time at which both are Connected.
std::uint64_t connect(FakeRadioBus& bus, Node& host, Node& client, std::uint64_t t0 = 0) {
    host.link.start_host("HOST", t0);
    client.link.start_scan(t0);
    std::uint64_t t = t0;
    bool joined = false;
    for (; t < t0 + 10000; t += kStep) {
        bus.advance_to(t);
        host.link.poll(t);
        client.link.poll(t);
        if (!joined && !client.link.peers().empty()) {
            joined = client.link.join(client.link.peers()[0].session_id, t);
        }
        if (host.link.state() == LinkState::Connected && client.link.state() == LinkState::Connected) break;
    }
    return t;
}

} // namespace

TEST(LoraLink, HostBeaconsPeriodically) {
    FakeRadioBus bus;
    Node host(bus, 1);
    host.link.start_host("HOST", 0);
    EXPECT_EQ(host.link.state(), LinkState::Beaconing);
    run(bus, {&host.link}, 0, 10000);
    const int n = count_type(bus, PacketType::Beacon);
    EXPECT_GE(n, 4);
    EXPECT_LE(n, 6);
}

TEST(LoraLink, ScanListsMultipleHosts) {
    FakeRadioBus bus;
    Node h1(bus, 11, -50);
    Node h2(bus, 22, -70);
    Node scanner(bus, 33, -60);
    h1.link.start_host("ALPHA", 0);
    h2.link.start_host("BRAVO", 0);
    scanner.link.start_scan(0);
    run(bus, {&h1.link, &h2.link, &scanner.link}, 0, 3000);
    ASSERT_EQ(scanner.link.peers().size(), 2u);
    std::map<std::string, LoRaPeer> by_name;
    for (const auto& p : scanner.link.peers()) by_name[p.host_name] = p;
    ASSERT_TRUE(by_name.count("ALPHA"));
    ASSERT_TRUE(by_name.count("BRAVO"));
    EXPECT_EQ(by_name["ALPHA"].session_id, h1.link.session_id());
    EXPECT_EQ(by_name["BRAVO"].session_id, h2.link.session_id());
    EXPECT_NE(h1.link.session_id(), h2.link.session_id());
    EXPECT_EQ(by_name["ALPHA"].rssi_dbm, -60);
}

TEST(LoraLink, PeerExpires) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node scanner(bus, 2);
    host.link.start_host("HOST", 0);
    scanner.link.start_scan(0);
    run(bus, {&host.link, &scanner.link}, 0, 3000);
    ASSERT_EQ(scanner.link.peers().size(), 1u);
    host.link.close(3000);
    EXPECT_EQ(host.link.state(), LinkState::Closed);
    run(bus, {&host.link, &scanner.link}, 3005, 3000 + 8500);
    EXPECT_TRUE(scanner.link.peers().empty());
}

TEST(LoraLink, JoinConnectsBothSides) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    const std::uint64_t t = connect(bus, host, client);
    EXPECT_EQ(host.link.state(), LinkState::Connected);
    EXPECT_EQ(client.link.state(), LinkState::Connected);
    EXPECT_EQ(client.link.session_id(), host.link.session_id());
    const int before = count_type(bus, PacketType::Beacon);
    run(bus, {&host.link, &client.link}, t + kStep, t + 6000);
    EXPECT_EQ(count_type(bus, PacketType::Beacon), before);
}

TEST(LoraLink, LostAcceptIsRecovered) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    bool dropped = false;
    bus.set_drop_filter([&](const std::vector<std::uint8_t>& b) {
        auto p = decode_packet(b.data(), b.size());
        if (p && p->type == PacketType::Accept && !dropped) {
            dropped = true;
            return true;
        }
        return false;
    });
    const std::uint64_t t = connect(bus, host, client);
    EXPECT_TRUE(dropped);
    EXPECT_EQ(host.link.state(), LinkState::Connected);
    EXPECT_EQ(client.link.state(), LinkState::Connected);
    EXPECT_LE(t, 3000u + 2000u); // beacon heard by ~0, join retry within ~1-2 s
}

TEST(LoraLink, DuplicateJoinDoesNotReset) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    const std::uint16_t sid = host.link.session_id();

    // Copy the real JOIN from the log and inject it again, plus one with a
    // different nonce.
    Packet join;
    for (const auto& r : bus.tx_log()) {
        auto p = decode_packet(r.bytes.data(), r.bytes.size());
        if (p && p->type == PacketType::Join) join = *p;
    }
    bus.inject_foreign(encode_packet(join));
    Packet other = join;
    other.nonce = static_cast<std::uint16_t>(join.nonce + 1);
    bus.inject_foreign(encode_packet(other));
    run(bus, {&host.link, &client.link}, t + kStep, t + 1000);
    t += 1000;
    EXPECT_EQ(host.link.session_id(), sid);
    EXPECT_EQ(host.link.state(), LinkState::Connected);
    EXPECT_EQ(client.link.state(), LinkState::Connected);

    auto d = payload13(7);
    ASSERT_TRUE(host.link.send_data(d.data(), d.size()));
    run(bus, {&host.link, &client.link}, t + kStep, t + 1000);
    std::vector<std::uint8_t> got;
    client.link.take_received(got);
    EXPECT_EQ(got, d);
}

TEST(LoraLink, DataFlowsBothWays) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    std::vector<std::uint8_t> all_h, all_c, exp_h, exp_c;
    for (int i = 0; i < 20; ++i) {
        auto a = payload13(static_cast<std::uint8_t>(i));
        auto b = payload13(static_cast<std::uint8_t>(100 + i));
        // The fake bus has no cross-radio carrier sense, so simultaneous
        // sends would collide (half duplex); stagger the two directions.
        ASSERT_TRUE(host.link.send_data(a.data(), a.size()));
        exp_c.insert(exp_c.end(), a.begin(), a.end());
        exp_h.insert(exp_h.end(), b.begin(), b.end());
        for (std::uint64_t u = 0; u < 600; u += kStep) {
            t += kStep;
            bus.advance_to(t);
            host.link.poll(t);
            client.link.poll(t);
            if (u == 300) ASSERT_TRUE(client.link.send_data(b.data(), b.size()));
        }
        host.link.take_received(all_h);
        client.link.take_received(all_c);
    }
    EXPECT_EQ(all_c, exp_c);
    EXPECT_EQ(all_h, exp_h);
}

TEST(LoraLink, SessionFilterIgnoresOtherMatches) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    Packet d;
    d.type = PacketType::Data;
    d.session_id = static_cast<std::uint16_t>(host.link.session_id() + 1);
    d.data = {1, 2, 3};
    Packet b;
    b.type = PacketType::Bye;
    b.session_id = d.session_id;
    bus.inject_foreign(encode_packet(d));
    bus.inject_foreign(encode_packet(b));
    run(bus, {&host.link, &client.link}, t + kStep, t + 500);
    std::vector<std::uint8_t> got;
    EXPECT_EQ(host.link.take_received(got), 0u);
    EXPECT_EQ(client.link.take_received(got), 0u);
    EXPECT_TRUE(got.empty());
    EXPECT_EQ(host.link.state(), LinkState::Connected);
    EXPECT_EQ(client.link.state(), LinkState::Connected);
}

TEST(LoraLink, ForeignPacketsAreIgnored) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::mt19937 rng(99);
    host.link.start_host("HOST", 0);
    client.link.start_scan(0);
    bool joined = false;
    std::vector<std::uint8_t> got;
    int injected = 0;
    int sent = 0;
    std::size_t expected = 0;
    // Junk keeps flowing (one packet per 25 ms) for the whole run, through
    // scanning, joining and data exchange.
    for (std::uint64_t t = 0; t < 25000 || injected < 500; t += kStep) {
        if (injected < 500 && t % 25 == 0) {
            std::vector<std::uint8_t> junk(1 + rng() % 40);
            for (auto& x : junk) x = static_cast<std::uint8_t>(rng());
            bus.inject_foreign(junk);
            ++injected;
        }
        bus.advance_to(t);
        host.link.poll(t);
        client.link.poll(t);
        if (!joined && !client.link.peers().empty()) {
            joined = client.link.join(client.link.peers()[0].session_id, t);
        }
        if (client.link.state() == LinkState::Connected && host.link.state() == LinkState::Connected &&
            t % 500 == 0 && sent < 20) {
            const auto d = payload13(static_cast<std::uint8_t>(sent));
            ASSERT_TRUE(host.link.send_data(d.data(), d.size()));
            expected += d.size();
            ++sent;
        }
        client.link.take_received(got);
    }
    EXPECT_EQ(injected, 500);
    EXPECT_EQ(sent, 20);
    EXPECT_EQ(got.size(), expected);
    EXPECT_EQ(host.link.state(), LinkState::Connected);
    EXPECT_EQ(client.link.state(), LinkState::Connected);
}

TEST(LoraLink, CarrierBusyBacksOff) {
    FakeRadioBus bus;
    Node host(bus, 1);
    host.link.start_host("HOST", 0);
    bus.set_channel_busy(true);
    run(bus, {&host.link}, 0, 3000);
    EXPECT_EQ(bus.tx_log().size(), 0u);
    bus.set_channel_busy(false);
    run(bus, {&host.link}, 3005, 3500);
    EXPECT_GE(bus.tx_log().size(), 1u);
}

TEST(LoraLink, PolicyDeferralRespected) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    // Burst of data from both sides.
    for (int i = 0; i < 12; ++i) {
        auto a = payload13(static_cast<std::uint8_t>(i));
        host.link.send_data(a.data(), a.size());
        client.link.send_data(a.data(), a.size());
    }
    run(bus, {&host.link, &client.link}, t + kStep, t + 8000);
    for (const IRadio* r : {static_cast<const IRadio*>(&host.radio), static_cast<const IRadio*>(&client.radio)}) {
        std::uint64_t prev = 0;
        std::size_t prev_bytes = 0;
        bool first = true;
        int n = 0;
        for (const auto& rec : bus.tx_log()) {
            if (rec.radio != r) continue;
            if (!first) {
                EXPECT_GE(rec.start_ms, prev + Japan920Policy{}.airtime_ms(prev_bytes) + 50u);
            }
            prev = rec.start_ms;
            prev_bytes = rec.bytes.size();
            first = false;
            ++n;
        }
        EXPECT_GE(n, 2);
    }
}

TEST(LoraLink, ByeClosesPeer) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    client.link.close(t);
    EXPECT_EQ(client.link.state(), LinkState::Closed);
    run(bus, {&host.link, &client.link}, t + kStep, t + 2000);
    EXPECT_EQ(host.link.state(), LinkState::Closed);
    EXPECT_EQ(count_type(bus, PacketType::Bye, &client.radio), 1);
}

TEST(LoraLink, JoinGivesUpWhenNoHost) {
    FakeRadioBus bus;
    Node client(bus, 2);
    client.link.start_scan(0);
    ASSERT_TRUE(client.link.join(0x1234, 0));
    EXPECT_EQ(client.link.state(), LinkState::Joining);
    run(bus, {&client.link}, 0, 4500);
    EXPECT_EQ(client.link.state(), LinkState::Joining);
    run(bus, {&client.link}, 4505, 6000);
    EXPECT_EQ(client.link.state(), LinkState::Failed);
    EXPECT_EQ(count_type(bus, PacketType::Join), 5);
}

TEST(LoraLink, CloseSendsByeDuringCall) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    // Let the link go quiet so policy/gap do not defer the BYE.
    run(bus, {&host.link, &client.link}, t + kStep, t + 1000);
    t += 1000;
    ASSERT_EQ(count_type(bus, PacketType::Bye), 0);
    client.link.close(t);
    EXPECT_EQ(client.link.state(), LinkState::Closed);
    EXPECT_EQ(count_type(bus, PacketType::Bye, &client.radio), 1); // already sent inside close()
    run(bus, {&host.link, &client.link}, t + kStep, t + 2000);
    EXPECT_EQ(count_type(bus, PacketType::Bye, &client.radio), 1);
    EXPECT_EQ(host.link.state(), LinkState::Closed);
}

TEST(LoraLink, PendingByeIsDroppedAfterWindow) {
    FakeRadioBus bus;
    Node host(bus, 1);
    Node client(bus, 2);
    std::uint64_t t = connect(bus, host, client);
    run(bus, {&host.link, &client.link}, t + kStep, t + 1000);
    t += 1000;
    const std::size_t before = bus.tx_log().size();
    bus.set_channel_busy(true);
    client.link.close(t);
    run(bus, {&host.link, &client.link}, t + kStep, t + 1000); // past the 500 ms window
    EXPECT_EQ(bus.tx_log().size(), before);
    EXPECT_EQ(client.link.state(), LinkState::Closed);
    bus.set_channel_busy(false);
    run(bus, {&host.link, &client.link}, t + 1005, t + 3000);
    EXPECT_EQ(bus.tx_log().size(), before); // no late BYE, nothing else either
    EXPECT_EQ(client.link.state(), LinkState::Closed);
}
