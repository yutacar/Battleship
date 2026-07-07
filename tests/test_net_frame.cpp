/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Frame codec tests: round-trips, CRC corruption detection, stream
 * fragmentation, and resynchronization after garbage (Phase 4, PLAN.md §2.2).
 */
#include <gtest/gtest.h>

#include "net/frame.h"
#include "net/messages.h"

namespace net = battleship::net;
namespace core = battleship::core;

namespace {

net::Frame make_frame(net::MessageType type, uint8_t seq, std::vector<uint8_t> payload) {
    net::Frame f;
    f.type = type;
    f.seq = seq;
    f.payload = std::move(payload);
    return f;
}

}  // namespace

TEST(NetFrame, EncodeProducesExpectedLayout) {
    const auto frame = make_frame(net::MessageType::Shot, 7, {3, 9});
    const auto bytes = net::encode_frame(frame);

    ASSERT_EQ(bytes.size(), net::kHeaderLen + 2 + net::kCrcLen);
    EXPECT_EQ(bytes[0], net::kMagicByte0);
    EXPECT_EQ(bytes[1], net::kMagicByte1);
    EXPECT_EQ(bytes[2], net::kProtocolVersion);
    EXPECT_EQ(bytes[3], static_cast<uint8_t>(net::MessageType::Shot));
    EXPECT_EQ(bytes[4], 7);
    EXPECT_EQ(bytes[5], 2);  // len
    EXPECT_EQ(bytes[6], 3);  // payload x
    EXPECT_EQ(bytes[7], 9);  // payload y
}

TEST(NetFrame, RoundTripAllMessageTypes) {
    const net::MessageType types[] = {
        net::MessageType::Hello, net::MessageType::Ready,  net::MessageType::Shot,
        net::MessageType::Result, net::MessageType::GameOver, net::MessageType::Ack,
        net::MessageType::Ping,
    };

    net::FrameDecoder decoder;
    uint8_t seq = 0;
    for (const auto type : types) {
        const auto original = make_frame(type, seq, {seq, static_cast<uint8_t>(seq + 1)});
        decoder.feed(net::encode_frame(original));

        const auto decoded = decoder.try_extract();
        ASSERT_TRUE(decoded.has_value()) << "type " << static_cast<int>(type);
        EXPECT_EQ(decoded->type, type);
        EXPECT_EQ(decoded->seq, seq);
        EXPECT_EQ(decoded->payload, original.payload);
        EXPECT_EQ(decoded->version, net::kProtocolVersion);
        ++seq;
    }
    EXPECT_FALSE(decoder.try_extract().has_value());
}

TEST(NetFrame, EmptyPayloadRoundTrip) {
    net::FrameDecoder decoder;
    decoder.feed(net::encode_frame(make_frame(net::MessageType::Ping, 42, {})));
    const auto decoded = decoder.try_extract();
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->type, net::MessageType::Ping);
    EXPECT_EQ(decoded->seq, 42);
    EXPECT_TRUE(decoded->payload.empty());
}

TEST(NetFrame, CrcCorruptionIsDetectedAndSkipped) {
    auto bytes = net::encode_frame(make_frame(net::MessageType::Shot, 1, {5, 5}));
    bytes[7] ^= 0xFF;  // corrupt a payload byte -> CRC mismatch

    net::FrameDecoder decoder;
    decoder.feed(bytes);
    EXPECT_FALSE(decoder.try_extract().has_value());
    EXPECT_GE(decoder.crc_failures(), 1);

    // A subsequent intact frame is still decodable (stream not poisoned).
    decoder.feed(net::encode_frame(make_frame(net::MessageType::Shot, 2, {1, 2})));
    const auto decoded = decoder.try_extract();
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->seq, 2);
}

TEST(NetFrame, HeaderCorruptionIsDetected) {
    auto bytes = net::encode_frame(make_frame(net::MessageType::Result, 9, {0, 0, 0, 0, 0}));
    bytes[3] = 0x77;  // corrupt the type field -- CRC covers the header too

    net::FrameDecoder decoder;
    decoder.feed(bytes);
    EXPECT_FALSE(decoder.try_extract().has_value());
    EXPECT_GE(decoder.crc_failures(), 1);
}

TEST(NetFrame, ByteWiseFragmentationReassembles) {
    const auto original = make_frame(net::MessageType::Result, 3, {4, 5, 1, 0, 1});
    const auto bytes = net::encode_frame(original);

    net::FrameDecoder decoder;
    for (size_t i = 0; i < bytes.size(); ++i) {
        // No complete frame until the very last byte arrives.
        if (i + 1 < bytes.size()) {
            decoder.feed(&bytes[i], 1);
            EXPECT_FALSE(decoder.try_extract().has_value()) << "byte " << i;
        } else {
            decoder.feed(&bytes[i], 1);
        }
    }
    const auto decoded = decoder.try_extract();
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->payload, original.payload);
}

TEST(NetFrame, ResynchronizesAfterLeadingGarbage) {
    const auto good = net::encode_frame(make_frame(net::MessageType::Hello, 0, {1, 200}));

    net::FrameDecoder decoder;
    // Garbage including a stray magic byte 0 (but not a full MAGIC pair).
    const std::vector<uint8_t> garbage = {0x00, 0xB5, 0x42, 0xFF, 0x13};
    decoder.feed(garbage);
    decoder.feed(good);

    const auto decoded = decoder.try_extract();
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->type, net::MessageType::Hello);
    EXPECT_EQ(decoded->payload, (std::vector<uint8_t>{1, 200}));
}

TEST(NetFrame, ResynchronizesAfterTruncatedFrame) {
    const auto first = net::encode_frame(make_frame(net::MessageType::Shot, 1, {2, 3}));
    const auto second = net::encode_frame(make_frame(net::MessageType::Shot, 2, {4, 5}));

    net::FrameDecoder decoder;
    // Only half of the first frame arrives (link dropped mid-frame). The
    // decoder cannot know the frame is truncated -- the byte where `len`
    // lands may claim a large payload -- so it waits for more bytes. What
    // matters is that continued traffic (in real use: the reliability
    // layer's retransmits of this very frame) eventually accumulates enough
    // bytes for the CRC check to fail, at which point the decoder resyncs
    // onto a subsequent intact frame instead of staying stuck forever.
    decoder.feed(first.data(), first.size() / 2);
    std::optional<net::Frame> decoded;
    for (int copies = 0; copies < 30 && !decoded; ++copies) {
        decoder.feed(second);
        decoded = decoder.try_extract();
    }
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->seq, 2);
    EXPECT_EQ(decoded->payload, (std::vector<uint8_t>{4, 5}));
    EXPECT_GE(decoder.crc_failures(), 1);
}

TEST(NetFrame, MagicPairSplitAcrossFeedsStillDecodes) {
    const auto bytes = net::encode_frame(make_frame(net::MessageType::Ready, 5, {}));

    net::FrameDecoder decoder;
    decoder.feed(bytes.data(), 1);  // just the first magic byte
    EXPECT_FALSE(decoder.try_extract().has_value());
    decoder.feed(bytes.data() + 1, bytes.size() - 1);
    const auto decoded = decoder.try_extract();
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->type, net::MessageType::Ready);
}

TEST(NetFrame, MultipleFramesInOneFeedDrainInOrder) {
    net::FrameDecoder decoder;
    std::vector<uint8_t> stream;
    for (uint8_t i = 0; i < 5; ++i) {
        const auto bytes = net::encode_frame(make_frame(net::MessageType::Shot, i, {i, i}));
        stream.insert(stream.end(), bytes.begin(), bytes.end());
    }
    decoder.feed(stream);

    for (uint8_t i = 0; i < 5; ++i) {
        const auto decoded = decoder.try_extract();
        ASSERT_TRUE(decoded.has_value()) << "frame " << static_cast<int>(i);
        EXPECT_EQ(decoded->seq, i);
    }
    EXPECT_FALSE(decoder.try_extract().has_value());
}

// ---------------------------------------------------------------- payloads

TEST(NetMessages, HelloRoundTrip) {
    const net::HelloPayload original{net::kProtocolVersion, 137};
    const auto decoded = net::decode_hello(net::encode(original));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->protocol_version, net::kProtocolVersion);
    EXPECT_EQ(decoded->tie_breaker, 137);
}

TEST(NetMessages, ShotRoundTrip) {
    const auto decoded = net::decode_shot(net::encode(net::ShotPayload{9, 0}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->x, 9);
    EXPECT_EQ(decoded->y, 0);
}

TEST(NetMessages, ResultRoundTrip) {
    net::ResultPayload original;
    original.x = 4;
    original.y = 7;
    original.outcome = core::ShotOutcome::Sunk;
    original.sunk_ship = core::ShipType::Carrier;
    original.game_won = 1;

    const auto decoded = net::decode_result(net::encode(original));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->x, 4);
    EXPECT_EQ(decoded->y, 7);
    EXPECT_EQ(decoded->outcome, core::ShotOutcome::Sunk);
    EXPECT_EQ(decoded->sunk_ship, core::ShipType::Carrier);
    EXPECT_EQ(decoded->game_won, 1);
}

TEST(NetMessages, MalformedPayloadsAreRejected) {
    EXPECT_FALSE(net::decode_hello({1}).has_value());          // too short
    EXPECT_FALSE(net::decode_shot({5}).has_value());           // too short
    EXPECT_FALSE(net::decode_result({1, 2, 3}).has_value());   // too short
    EXPECT_FALSE(net::decode_result({1, 2, 99, 0, 0}).has_value());  // bad outcome
    EXPECT_FALSE(net::decode_result({1, 2, 0, 99, 0}).has_value());  // bad ship type
    EXPECT_FALSE(net::decode_ack({}).has_value());             // empty
    EXPECT_FALSE(net::decode_game_over({}).has_value());       // empty
}

TEST(NetMessages, PayloadsStayTinyForLoRa) {
    // PLAN.md §2.2: payloads must remain a few bytes so the protocol works
    // over a low-bandwidth LoRa link without change. Guard against payload
    // growth sneaking in.
    EXPECT_LE(net::encode(net::HelloPayload{}).size(), 4u);
    EXPECT_LE(net::encode(net::ShotPayload{}).size(), 4u);
    EXPECT_LE(net::encode(net::ResultPayload{}).size(), 8u);
    EXPECT_LE(net::encode(net::GameOverPayload{}).size(), 4u);
    EXPECT_LE(net::encode(net::AckPayload{}).size(), 4u);
    EXPECT_TRUE(net::encode(net::ReadyPayload{}).empty());
    EXPECT_TRUE(net::encode(net::PingPayload{}).empty());
}

TEST(NetMessages, EveryEncodedFrameFitsOneBleWriteAtDefaultMtu) {
    // Phase 6 (PLAN.md §2.2 BleTransport row): BleTransport maps one encoded
    // frame onto one ATT Write/Notify and never negotiates the MTU, so every
    // possible frame must fit the BT 4.2 default of 20 usable bytes. The
    // compile-time guarantee lives in messages.h (static_assert on
    // kMaxGameFrameLen); this dynamically re-checks the real encoders in
    // case a payload grows without its struct definition changing size.
    const std::vector<std::vector<uint8_t>> payloads = {
        net::encode(net::HelloPayload{net::kProtocolVersion, 0xFF}),
        net::encode(net::ReadyPayload{}),
        net::encode(net::ShotPayload{9, 9}),
        net::encode(net::ResultPayload{9, 9, core::ShotOutcome::Sunk,
                                       core::ShipType::Carrier, 1}),
        net::encode(net::GameOverPayload{1}),
        net::encode(net::AckPayload{0xFF}),
        net::encode(net::PingPayload{}),
    };
    for (const auto& payload : payloads) {
        EXPECT_LE(payload.size(), net::kMaxGameMessagePayloadLen);
        const auto encoded =
            net::encode_frame(make_frame(net::MessageType::Ping, 0xFF, payload));
        EXPECT_LE(encoded.size(), net::kBleSingleWriteLimit)
            << "frame with a " << payload.size()
            << "-byte payload no longer fits a single BLE write/notify";
        EXPECT_LE(encoded.size(), net::kMaxGameFrameLen);
    }
}
