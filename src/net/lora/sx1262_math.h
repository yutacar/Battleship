/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Pure SX1262 helpers (no hardware access) so the arithmetic that the Linux
 * driver depends on can be unit-tested on desktop.
 */

#pragma once

#include <array>
#include <cstdint>

namespace battleship::net::lora {

// GetRssiInst / GetPacketStatus report -value/2 dBm. Truncation is toward
// zero (e.g. raw 181 -> -90 dBm), i.e. toward the stronger reading.
constexpr std::int16_t sx1262_rssi_dbm(std::uint8_t raw) {
    return static_cast<std::int16_t>(-static_cast<int>(raw) / 2);
}

// Carrier sense: busy when the instantaneous RSSI is at or above threshold.
constexpr bool sx1262_carrier_busy(std::uint8_t raw_rssi, std::int16_t threshold_dbm) {
    return sx1262_rssi_dbm(raw_rssi) >= threshold_dbm;
}

// LoRa sync word register (0x0740/0x0741) bytes for an 8-bit sync word,
// e.g. 0x12 -> {0x14, 0x24}, 0x34 -> {0x34, 0x44}.
constexpr std::array<std::uint8_t, 2> sx1262_sync_word_bytes(std::uint8_t sync) {
    return {static_cast<std::uint8_t>((sync & 0xF0U) | 0x04U),
            static_cast<std::uint8_t>(((sync & 0x0FU) << 4U) | 0x04U)};
}

// RfFreq = freq * 2^25 / 32 MHz.
constexpr std::uint32_t sx1262_frequency_word(std::uint32_t frequency_hz) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(frequency_hz) << 25U) /
                                      32'000'000ULL);
}

// ARIB STD-T108 listen-before-talk limits for Sx1262Config::valid():
// carrier sense of at least 5 ms, threshold no higher than -80 dBm.
constexpr bool sx1262_lbt_config_valid(std::uint32_t carrier_sense_ms,
                                       std::int16_t lbt_threshold_dbm) {
    return carrier_sense_ms >= 5U && lbt_threshold_dbm <= -80;
}

// GetRssiInst reads a stale value (the previous packet's AGC level) for a
// short time after SetRx; samples are only valid this long after re-arm.
inline constexpr std::uint32_t kRssiSettleUs = 1000U;

// Remaining wait before carrier-sense RSSI samples are valid; 0 when settled.
constexpr std::uint32_t sx1262_cs_settle_wait_us(std::uint32_t us_since_rearm) {
    return us_since_rearm >= kRssiSettleUs ? 0U : kRssiSettleUs - us_since_rearm;
}

} // namespace battleship::net::lora
