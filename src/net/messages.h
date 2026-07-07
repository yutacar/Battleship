/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Typed payload encode/decode for each MessageType (PLAN.md §2.2). Every
 * payload is a handful of bytes so a single frame comfortably fits within
 * kMaxPayloadLen even over a future low-bandwidth LoRa transport. We never
 * send board contents -- only coordinates and outcomes -- so a captured
 * packet reveals nothing about ship placement beyond what the receiving
 * player would already deduce locally (PLAN.md §2.2 "相手の盤面情報は送らず").
 */
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/types.h"
#include "net/frame.h"

namespace battleship::net {

/// HELLO: exchanged once per connection. `protocol_version` lets a future
/// incompatible peer refuse to proceed cleanly; `tie_breaker` is a random
/// byte compared between peers to decide who fires first (higher wins;
/// ties are vanishingly rare across the 0-255 range but callers should
/// re-roll/re-HELLO on an exact tie, see NetSession).
struct HelloPayload {
    uint8_t protocol_version = kProtocolVersion;
    uint8_t tie_breaker = 0;
};

/// READY: sender has finished placing their fleet. No payload fields beyond
/// the frame header are needed; kept as a struct for API symmetry/future
/// extension.
struct ReadyPayload {};

/// SHOT(x, y): a shot fired at the receiver's board.
struct ShotPayload {
    uint8_t x = 0;
    uint8_t y = 0;
};

/// RESULT: outcome of a previously-sent SHOT, addressed by coordinate (not
/// by seq) so it stays meaningful even if the reliability layer's seq
/// numbering differs between peers.
struct ResultPayload {
    uint8_t x = 0;
    uint8_t y = 0;
    battleship::core::ShotOutcome outcome = battleship::core::ShotOutcome::Miss;
    battleship::core::ShipType sunk_ship = battleship::core::ShipType::Destroyer;  // valid iff outcome==Sunk
    uint8_t game_won = 0;  // 0/1
};

/// GAME_OVER: redundant with the game-ending RESULT's game_won flag, but
/// sent explicitly so a peer that missed/mis-sequenced the final RESULT (or
/// wants an unambiguous "match is over, stop sending shots" signal) still
/// converges. Carries the winner from the sender's point of view.
struct GameOverPayload {
    uint8_t winner_is_sender = 0;  // 1 if the sender of this message won
};

/// ACK: acknowledges a specific seq number from the reliability layer.
struct AckPayload {
    uint8_t acked_seq = 0;
};

/// PING: liveness probe. No payload.
struct PingPayload {};

/// Largest payload any MessageType above encodes to (ResultPayload: x, y,
/// outcome, sunk_ship, game_won = 5 bytes). Kept as an explicit constant so
/// the BLE single-PDU guarantee below breaks the build (not a device in the
/// field) if someone grows a payload past it. test_net_frame.cpp verifies at
/// runtime that every encode() actually fits.
constexpr size_t kMaxGameMessagePayloadLen = 5;

/// Worst-case encoded frame: header + largest game payload + CRC.
constexpr size_t kMaxGameFrameLen = kHeaderLen + kMaxGameMessagePayloadLen + kCrcLen;

/// BLE constraint (Phase 6, PLAN.md §2.2 BleTransport row): every frame must
/// fit in a single ATT Write/Notify at the BT 4.2 default MTU (23 -> 20
/// usable bytes), so BleTransport needs no fragmentation/reassembly and no
/// MTU negotiation. 6 + 5 + 2 = 13 bytes today -- comfortable headroom.
constexpr size_t kBleSingleWriteLimit = 20;
static_assert(kMaxGameFrameLen <= kBleSingleWriteLimit,
              "A game frame no longer fits one BLE write/notify at the default MTU; "
              "shrink the payload or add a fragmentation layer to BleTransport");

std::vector<uint8_t> encode(const HelloPayload& p);
std::vector<uint8_t> encode(const ReadyPayload& p);
std::vector<uint8_t> encode(const ShotPayload& p);
std::vector<uint8_t> encode(const ResultPayload& p);
std::vector<uint8_t> encode(const GameOverPayload& p);
std::vector<uint8_t> encode(const AckPayload& p);
std::vector<uint8_t> encode(const PingPayload& p);

std::optional<HelloPayload> decode_hello(const std::vector<uint8_t>& bytes);
std::optional<ReadyPayload> decode_ready(const std::vector<uint8_t>& bytes);
std::optional<ShotPayload> decode_shot(const std::vector<uint8_t>& bytes);
std::optional<ResultPayload> decode_result(const std::vector<uint8_t>& bytes);
std::optional<GameOverPayload> decode_game_over(const std::vector<uint8_t>& bytes);
std::optional<AckPayload> decode_ack(const std::vector<uint8_t>& bytes);
std::optional<PingPayload> decode_ping(const std::vector<uint8_t>& bytes);

}  // namespace battleship::net
