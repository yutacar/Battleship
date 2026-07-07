/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/messages.h"

namespace battleship::net {

using battleship::core::ShipType;
using battleship::core::ShotOutcome;

namespace {
uint8_t to_byte(ShotOutcome outcome) { return static_cast<uint8_t>(outcome); }
uint8_t to_byte(ShipType type) { return static_cast<uint8_t>(type); }

std::optional<ShotOutcome> outcome_from_byte(uint8_t b) {
    switch (b) {
        case static_cast<uint8_t>(ShotOutcome::Miss):
        case static_cast<uint8_t>(ShotOutcome::Hit):
        case static_cast<uint8_t>(ShotOutcome::Sunk):
        case static_cast<uint8_t>(ShotOutcome::AlreadyShot):
        case static_cast<uint8_t>(ShotOutcome::OutOfBounds):
            return static_cast<ShotOutcome>(b);
        default:
            return std::nullopt;
    }
}

std::optional<ShipType> ship_type_from_byte(uint8_t b) {
    switch (b) {
        case static_cast<uint8_t>(ShipType::Carrier):
        case static_cast<uint8_t>(ShipType::Battleship):
        case static_cast<uint8_t>(ShipType::Cruiser):
        case static_cast<uint8_t>(ShipType::Submarine):
        case static_cast<uint8_t>(ShipType::Destroyer):
            return static_cast<ShipType>(b);
        default:
            return std::nullopt;
    }
}
}  // namespace

std::vector<uint8_t> encode(const HelloPayload& p) { return {p.protocol_version, p.tie_breaker}; }
std::vector<uint8_t> encode(const ReadyPayload&) { return {}; }
std::vector<uint8_t> encode(const ShotPayload& p) { return {p.x, p.y}; }

std::vector<uint8_t> encode(const ResultPayload& p) {
    return {p.x, p.y, to_byte(p.outcome), to_byte(p.sunk_ship), p.game_won};
}

std::vector<uint8_t> encode(const GameOverPayload& p) { return {p.winner_is_sender}; }
std::vector<uint8_t> encode(const AckPayload& p) { return {p.acked_seq}; }
std::vector<uint8_t> encode(const PingPayload&) { return {}; }

std::optional<HelloPayload> decode_hello(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 2) return std::nullopt;
    return HelloPayload{bytes[0], bytes[1]};
}

std::optional<ReadyPayload> decode_ready(const std::vector<uint8_t>&) { return ReadyPayload{}; }

std::optional<ShotPayload> decode_shot(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 2) return std::nullopt;
    return ShotPayload{bytes[0], bytes[1]};
}

std::optional<ResultPayload> decode_result(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 5) return std::nullopt;
    const auto outcome = outcome_from_byte(bytes[2]);
    const auto sunk_ship = ship_type_from_byte(bytes[3]);
    if (!outcome || !sunk_ship) return std::nullopt;
    ResultPayload p;
    p.x = bytes[0];
    p.y = bytes[1];
    p.outcome = *outcome;
    p.sunk_ship = *sunk_ship;
    p.game_won = bytes[4];
    return p;
}

std::optional<GameOverPayload> decode_game_over(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return std::nullopt;
    return GameOverPayload{bytes[0]};
}

std::optional<AckPayload> decode_ack(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return std::nullopt;
    return AckPayload{bytes[0]};
}

std::optional<PingPayload> decode_ping(const std::vector<uint8_t>&) { return PingPayload{}; }

}  // namespace battleship::net
