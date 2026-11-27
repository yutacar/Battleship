/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/lora_packet.h"

#include <algorithm>
#include <cstring>
#include <random>

#include <gtest/gtest.h>

#include "support/fake_radio_bus.h"

using namespace battleship::net::lora;
using battleship::net::lora::testing::FakeRadioBus;

namespace {
Packet roundtrip(const Packet& p) {
    auto bytes = encode_packet(p);
    auto d = decode_packet(bytes.data(), bytes.size());
    EXPECT_TRUE(d.has_value());
    return d.value_or(Packet{});
}
} // namespace

TEST(LoraPacket, RoundTripAllTypes) {
    Packet b;
    b.type = PacketType::Beacon;
    b.session_id = 0xABCD;
    b.flags = 0x05;
    b.host_name = "Admiral";
    Packet r = roundtrip(b);
    EXPECT_EQ(r.type, PacketType::Beacon);
    EXPECT_EQ(r.session_id, 0xABCD);
    EXPECT_EQ(r.flags, 0x05);
    EXPECT_EQ(r.host_name, "Admiral");

    for (PacketType t : {PacketType::Join, PacketType::Accept}) {
        Packet p;
        p.type = t;
        p.session_id = 0x1234;
        p.nonce = 0xBEEF;
        Packet q = roundtrip(p);
        EXPECT_EQ(q.type, t);
        EXPECT_EQ(q.session_id, 0x1234);
        EXPECT_EQ(q.nonce, 0xBEEF);
    }

    Packet d;
    d.type = PacketType::Data;
    d.session_id = 7;
    d.data = {1, 2, 3, 0, 255};
    Packet dq = roundtrip(d);
    EXPECT_EQ(dq.type, PacketType::Data);
    EXPECT_EQ(dq.session_id, 7);
    EXPECT_EQ(dq.data, d.data);

    Packet bye;
    bye.type = PacketType::Bye;
    bye.session_id = 99;
    Packet byq = roundtrip(bye);
    EXPECT_EQ(byq.type, PacketType::Bye);
    EXPECT_EQ(byq.session_id, 99);
}

TEST(LoraPacket, BeaconCarriesReadableAd) {
    Packet b;
    b.type = PacketType::Beacon;
    b.host_name = "Admiral";
    auto bytes = encode_packet(b);
    EXPECT_LE(bytes.size(), kMaxRadioPacket);
    std::string s(bytes.begin(), bytes.end());
    EXPECT_NE(s.find("BATTLESHIP"), std::string::npos);
    EXPECT_NE(s.find("CardputerZero"), std::string::npos);
    EXPECT_NE(s.find("BSHP"), std::string::npos);
}

TEST(LoraPacket, BeaconTruncatesLongHostName) {
    Packet b;
    b.type = PacketType::Beacon;
    b.host_name = "ABCDEFGHIJKLMNOPQRST";
    Packet r = roundtrip(b);
    EXPECT_EQ(r.host_name, "ABCDEFGHIJKL");
}

TEST(LoraPacket, DecodeRejectsGarbage) {
    std::mt19937 rng(12345);
    for (int i = 0; i < 10000; ++i) {
        std::vector<std::uint8_t> v(rng() % 81);
        for (auto& x : v) x = static_cast<std::uint8_t>(rng());
        auto d = decode_packet(v.data(), v.size());
        if (v.size() < 2 || v[0] != 'B' || v[1] != 'S') {
            EXPECT_FALSE(d.has_value());
        }
    }
}

TEST(LoraPacket, DecodeRejectsWrongLengths) {
    auto make = [](PacketType t, std::size_t body) {
        std::vector<std::uint8_t> v = {'B', 'S', static_cast<std::uint8_t>(t), 1, 0};
        v.resize(5 + body, 0x41);
        return v;
    };
    auto bad = [](const std::vector<std::uint8_t>& v) {
        return !decode_packet(v.data(), v.size()).has_value();
    };
    EXPECT_TRUE(bad(make(PacketType::Join, 1)));   // len 6
    EXPECT_TRUE(bad(make(PacketType::Join, 3)));
    EXPECT_TRUE(bad(make(PacketType::Data, 0)));   // len 5
    EXPECT_TRUE(bad(make(PacketType::Data, 17)));  // len 22
    EXPECT_TRUE(bad(make(PacketType::Bye, 1)));
    EXPECT_FALSE(bad(make(PacketType::Join, 2)));
    EXPECT_FALSE(bad(make(PacketType::Data, 16)));
}

TEST(LoraPacket, DecodeRejectsBadBeacon) {
    Packet b;
    b.type = PacketType::Beacon;
    b.host_name = "Admiral";
    auto bytes = encode_packet(b);
    auto wrong_ad = bytes;
    wrong_ad.back() ^= 1;
    EXPECT_FALSE(decode_packet(wrong_ad.data(), wrong_ad.size()).has_value());
    auto bad_name = bytes;
    bad_name[6] = 0x01; // non-printable in the name
    EXPECT_FALSE(decode_packet(bad_name.data(), bad_name.size()).has_value());
}

TEST(LoraPacket, FakeBusHalfDuplex) {
    FakeRadioBus bus;
    auto& a = bus.add_radio();
    auto& b = bus.add_radio();
    const std::uint8_t pa[] = {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A'};
    const std::uint8_t pb[] = {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'};
    ASSERT_EQ(a.try_start_transmit(pa, sizeof(pa)), RadioStart::Started);
    bus.advance_to(5); // A still on air
    ASSERT_EQ(b.try_start_transmit(pb, sizeof(pb)), RadioStart::Started);
    bus.advance_to(1000);

    std::uint8_t buf[kMaxRadioPacket];
    RadioPoll pa_ev = a.poll(buf, sizeof(buf));
    EXPECT_EQ(pa_ev.event, RadioEvent::TxDone);
    EXPECT_EQ(a.poll(buf, sizeof(buf)).event, RadioEvent::Idle); // heard nothing from B
    RadioPoll pb_ev = b.poll(buf, sizeof(buf));
    EXPECT_EQ(pb_ev.event, RadioEvent::TxDone);
    RadioPoll got = b.poll(buf, sizeof(buf));
    ASSERT_EQ(got.event, RadioEvent::Received);
    EXPECT_EQ(got.size, sizeof(pa));
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(b.poll(buf, sizeof(buf)).event, RadioEvent::Idle);
}

TEST(LoraPacket, FakeBusChannelBusyAndForeign) {
    FakeRadioBus bus;
    auto& a = bus.add_radio();
    auto& b = bus.add_radio(-80);
    const std::uint8_t p[] = {1, 2, 3};
    bus.set_channel_busy(true);
    EXPECT_EQ(a.try_start_transmit(p, sizeof(p)), RadioStart::Busy);
    bus.set_channel_busy(false);
    bus.inject_foreign({9, 9});
    std::uint8_t buf[kMaxRadioPacket];
    RadioPoll r = b.poll(buf, sizeof(buf));
    EXPECT_EQ(r.event, RadioEvent::Received);
    EXPECT_EQ(r.rssi_dbm, -80);
    EXPECT_EQ(r.size, 2u);
}
