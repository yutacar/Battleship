/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * LoRaTransport over LoRaLink over the fake radio bus: full NetSession games
 * with real SF7 airtime, round-trip latency and silent-peer detection.
 */

#include "net/lora/lora_transport.h"

#include <cstdint>
#include <memory>
#include <random>

#include <gtest/gtest.h>

#include "core/fleet.h"
#include "net/lora/lora_link.h"
#include "support/fake_radio_bus.h"
#include "support/net_game_driver.h"

namespace net = battleship::net;
namespace core = battleship::core;
using namespace battleship::net::lora;
using battleship::net::lora::testing::FakeBusConfig;
using battleship::net::lora::testing::FakeRadio;
using battleship::net::lora::testing::FakeRadioBus;

namespace {

constexpr uint64_t kStep = 5;

struct Rig {
    FakeRadioBus bus;
    FakeRadio& radio_host;
    FakeRadio& radio_client;
    LoRaLink link_host;
    LoRaLink link_client;
    uint64_t now = 0;
    LoRaTransport transport_host;
    LoRaTransport transport_client;
    std::unique_ptr<net::NetSession> host;
    std::unique_ptr<net::NetSession> client;

    Rig(double loss, uint32_t seed)
        : bus(FakeBusConfig{loss, seed, true, true}),
          radio_host(bus.add_radio()),
          radio_client(bus.add_radio()),
          link_host(radio_host, Japan920Policy{}, seed * 2 + 1),
          link_client(radio_client, Japan920Policy{}, seed * 2 + 2),
          transport_host(link_host, [this] { return now; }),
          transport_client(link_client, [this] { return now; }) {}

    void make_sessions(uint32_t seed) {
        host = std::make_unique<net::NetSession>(transport_host, net::SessionRole::Host,
                                                 seed * 2 + 1, lora_session_config());
        client = std::make_unique<net::NetSession>(transport_client, net::SessionRole::Client,
                                                   seed * 2 + 2, lora_session_config());
    }

    // One simulated tick at absolute time t; sessions are polled when present.
    void tick(uint64_t t) {
        now = t;
        bus.advance_to(now);
        link_host.poll(now);
        link_client.poll(now);
        if (host) host->poll(static_cast<uint32_t>(now));
        if (client) client->poll(static_cast<uint32_t>(now));
    }

    // Beacon -> scan -> join until both links are Connected. Returns false on timeout.
    bool connect(uint64_t limit_ms = 30000) {
        link_host.start_host("HOST", now);
        link_client.start_scan(now);
        bool joined = false;
        for (uint64_t t = now; t <= limit_ms; t += kStep) {
            tick(t);
            if (!joined && !link_client.peers().empty()) {
                joined = link_client.join(link_client.peers().front().session_id, now);
            }
            if (link_host.state() == LinkState::Connected &&
                link_client.state() == LinkState::Connected) {
                return true;
            }
        }
        return false;
    }
};

// Plays a whole game over the rig (links already connected).
void play_full_game(Rig& rig, uint32_t seed) {
    const uint64_t base = rig.now;
    battleship::testing::run_full_game(
        *rig.host, *rig.client, [&](uint32_t t) { rig.tick(base + t); }, seed,
        static_cast<uint32_t>(kStep));
}

} // namespace

TEST(LoRaTransport, ReportsConnectingThenConnected) {
    Rig rig(0.0, 1);
    EXPECT_EQ(rig.transport_host.state(), net::TransportState::Connecting);
    EXPECT_EQ(rig.transport_client.state(), net::TransportState::Connecting);
    ASSERT_TRUE(rig.connect());
    EXPECT_EQ(rig.transport_host.state(), net::TransportState::Connected);
    EXPECT_EQ(rig.transport_client.state(), net::TransportState::Connected);

    rig.transport_host.close();
    EXPECT_EQ(rig.transport_host.state(), net::TransportState::Disconnected);
}

TEST(LoRaTransport, SendSplitsFramesLargerThanOnePacket) {
    Rig rig(0.0, 1);
    ASSERT_TRUE(rig.connect());
    std::vector<uint8_t> data(40);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
    EXPECT_TRUE(rig.transport_host.send(data));
    std::vector<uint8_t> got;
    const uint64_t end = rig.now + 3000;
    for (uint64_t t = rig.now; t < end && got.size() < data.size(); t += kStep) {
        rig.tick(t);
        rig.transport_client.receive(got);
    }
    EXPECT_EQ(got, data);
}

TEST(LoRaTransport, FullGameOverFakeLoRaCleanLink) {
    for (uint32_t seed = 0; seed < 5; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        Rig rig(0.0, seed);
        ASSERT_TRUE(rig.connect());
        rig.make_sessions(seed);
        play_full_game(rig, seed);
        std::printf("[game] seed %u: %llu ms sim, %zu packets\n", seed, (unsigned long long)rig.now, rig.bus.tx_log().size());
        battleship::testing::expect_consistent_final_state(*rig.host, *rig.client);
    }
}

TEST(LoRaTransport, FullGameOverFakeLoRaLossyLink) {
    for (uint32_t seed = 0; seed < 4; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        Rig rig(0.10, seed);
        ASSERT_TRUE(rig.connect());
        rig.make_sessions(seed);
        play_full_game(rig, seed);
        battleship::testing::expect_consistent_final_state(*rig.host, *rig.client);
    }
}

namespace {

// Brings both sessions to Battle with fleets placed.
void reach_battle(Rig& rig, uint32_t seed) {
    std::mt19937 rng_h(seed + 11), rng_c(seed + 22);
    bool hp = false, cp = false;
    const uint64_t end = rig.now + 60000;
    for (uint64_t t = rig.now; t < end; t += kStep) {
        rig.tick(t);
        if (!hp && rig.host->phase() == net::SessionPhase::Placement) {
            core::place_fleet_randomly(rig.host->player_view().own_board(), rng_h);
            ASSERT_TRUE(rig.host->send_ready());
            hp = true;
        }
        if (!cp && rig.client->phase() == net::SessionPhase::Placement) {
            core::place_fleet_randomly(rig.client->player_view().own_board(), rng_c);
            ASSERT_TRUE(rig.client->send_ready());
            cp = true;
        }
        if (rig.host->phase() == net::SessionPhase::Battle &&
            rig.client->phase() == net::SessionPhase::Battle) {
            return;
        }
    }
    FAIL() << "never reached Battle";
}

} // namespace

TEST(LoRaTransport, ShotRoundTripUnderOneSecond) {
    for (uint32_t seed = 0; seed < 5; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        Rig rig(0.0, seed);
        ASSERT_TRUE(rig.connect());
        rig.make_sessions(seed);
        reach_battle(rig, seed);

        net::NetSession& shooter = rig.host->can_fire() ? *rig.host : *rig.client;
        // Let any in-flight ACK traffic settle so we measure a quiet link.
        const uint64_t settle_end = rig.now + 1000;
        uint64_t t = rig.now;
        for (; t < settle_end; t += kStep) rig.tick(t);
        ASSERT_TRUE(shooter.can_fire());

        const uint64_t t0 = t;
        ASSERT_TRUE(shooter.fire({0, 0}));
        uint64_t t_result = 0;
        for (; t < t0 + 5000; t += kStep) {
            rig.tick(t);
            if (!shooter.pending_shot().has_value()) {
                t_result = t;
                break;
            }
        }
        ASSERT_NE(t_result, 0u) << "result never arrived";
        std::printf("[round trip] seed %u: %llu ms\n", seed,
                    static_cast<unsigned long long>(t_result - t0));
        EXPECT_LT(t_result - t0, 1000u);
    }
}

TEST(LoRaTransport, SilentPeerDisconnectsWithinTimeout) {
    Rig rig(0.0, 3);
    ASSERT_TRUE(rig.connect());
    rig.make_sessions(3);
    reach_battle(rig, 3);

    // Settle, then the client's radio dies: it neither sends nor receives.
    const uint64_t settle_end = rig.now + 500;
    uint64_t t = rig.now;
    for (; t < settle_end; t += kStep) rig.tick(t);
    rig.radio_client.shutdown();

    // Host keeps being polled; the client is no longer polled either.
    uint64_t last_rx = t; // upper bound: nothing the client sends can arrive after this
    uint64_t t_disc = 0;
    for (; t < last_rx + 60000; t += kStep) {
        rig.now = t;
        rig.bus.advance_to(t);
        rig.link_host.poll(t);
        rig.host->poll(static_cast<uint32_t>(t));
        if (rig.host->phase() == net::SessionPhase::Disconnected) {
            t_disc = t;
            break;
        }
    }
    ASSERT_NE(t_disc, 0u) << "host never noticed the silent peer";
    std::printf("[silent peer] disconnected %llu ms after last possible packet\n",
                static_cast<unsigned long long>(t_disc - last_rx));
    EXPECT_LE(t_disc - last_rx, 20000u + kStep);
}

// ---------------------------------------------------------------------------
// UdpSimRadio: two instances in one process over real multicast sockets.
// ---------------------------------------------------------------------------
#include <chrono>
#include <thread>

#include "net/lora/radio_factory.h"
#include "net/lora/udp_sim_radio.h"

TEST(LoraUdpSim, UdpSimTwoInstancesExchange) {
    constexpr uint16_t kPort = 35361;
    UdpSimRadio ra(0.0, kPort);
    UdpSimRadio rb(0.0, kPort);
    if (!ra.ready() || !rb.ready()) GTEST_SKIP() << "multicast socket unavailable";
    {
        // Environmental probe: skip only if a raw datagram never arrives.
        const uint8_t probe[] = {0xAA};
        ASSERT_EQ(ra.try_start_transmit(probe, 1), RadioStart::Started);
        uint8_t buf[kMaxRadioPacket];
        bool arrived = false;
        for (int i = 0; i < 250 && !arrived; ++i) {
            arrived = rb.poll(buf, sizeof(buf)).event == RadioEvent::Received;
            if (!arrived) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!arrived) GTEST_SKIP() << "multicast loopback delivery unavailable";
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let TxDone come due
        while (ra.poll(buf, sizeof(buf)).event != RadioEvent::Idle) {}
    }

    LoRaLink host(ra, Japan920Policy{}, 11);
    LoRaLink joiner(rb, Japan920Policy{}, 22);

    const auto t0 = std::chrono::steady_clock::now();
    auto now_ms = [&] {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - t0).count());
    };
    host.start_host("SIM", now_ms());
    joiner.start_scan(now_ms());

    bool joined_called = false;
    bool host_got = false, joiner_got = false, sent = false;
    std::vector<uint8_t> in_h, in_j;
    const uint8_t ping[] = {1, 2, 3}, pong[] = {9, 8, 7, 6};
    while (now_ms() < 10000 && !(host_got && joiner_got)) {
        host.poll(now_ms());
        joiner.poll(now_ms());
        if (!joined_called && !joiner.peers().empty()) {
            joined_called = joiner.join(joiner.peers()[0].session_id, now_ms());
        }
        if (!sent && host.state() == LinkState::Connected &&
            joiner.state() == LinkState::Connected) {
            sent = host.send_data(ping, sizeof(ping)) && joiner.send_data(pong, sizeof(pong));
        }
        if (host.take_received(in_h) > 0) host_got = true;
        if (joiner.take_received(in_j) > 0) joiner_got = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(joined_called) << "scan never discovered the host";
    EXPECT_EQ(host.state(), LinkState::Connected);
    EXPECT_EQ(joiner.state(), LinkState::Connected);
    ASSERT_TRUE(host_got);
    ASSERT_TRUE(joiner_got);
    EXPECT_EQ(in_h, std::vector<uint8_t>(pong, pong + sizeof(pong)));
    EXPECT_EQ(in_j, std::vector<uint8_t>(ping, ping + sizeof(ping)));
}

TEST(LoraUdpSim, FactoryHardwareUnavailableOrSim) {
    auto h = open_radio(RadioBackend::UdpSim);
    EXPECT_NE(h.radio, nullptr);
    EXPECT_EQ(h.power, nullptr);
#if !BATTLESHIP_LORA_HW
    EXPECT_EQ(default_radio_backend(), RadioBackend::UdpSim);
    auto hw = open_radio(RadioBackend::Hardware);
    EXPECT_EQ(hw.radio, nullptr);
    EXPECT_EQ(hw.error, RadioInitError::SpiOpenFailed);
#endif
}

TEST(LoraUdpSim, DoesNotReceiveOwnPacket) {
    UdpSimRadio r(0.0, 35362);
    if (!r.ready()) GTEST_SKIP() << "multicast socket unavailable";
    const uint8_t p[] = {1, 2, 3};
    ASSERT_EQ(r.try_start_transmit(p, 3), RadioStart::Started);
    EXPECT_EQ(r.try_start_transmit(p, 3), RadioStart::Busy);
    uint8_t buf[kMaxRadioPacket];
    bool tx_done = false;
    for (int i = 0; i < 100; ++i) {
        RadioPoll rp = r.poll(buf, sizeof(buf));
        ASSERT_NE(rp.event, RadioEvent::Received);
        if (rp.event == RadioEvent::TxDone) tx_done = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    EXPECT_TRUE(tx_done);
}
