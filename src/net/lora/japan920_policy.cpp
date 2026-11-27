/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Ported from LoraMessenger japan_920_radio_policy.cpp (MIT, same author).
 */

#include "net/lora/japan920_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace battleship::net::lora {
namespace {

constexpr std::size_t kMaximumDatagramBytes = 255U;

RadioTick saturating_add(RadioTick left, RadioTick right) noexcept {
    const auto maximum = std::numeric_limits<RadioTick>::max();
    return left > maximum - right ? maximum : left + right;
}

} // namespace

bool Japan920Profile::valid() const noexcept {
    constexpr std::uint32_t kCertifiedLowerHz = 920'500'000U;
    // The published Cap LoRa-1262 range ends at 923 MHz. Keep the entire
    // occupied 125 kHz channel inside that documented hardware range.
    constexpr std::uint32_t kCapUpperHz = 923'000'000U;
    if (bandwidth_hz != 125'000U ||
        spreading_factor < 7U || spreading_factor > 12U ||
        coding_rate_denominator < 5U || coding_rate_denominator > 8U ||
        transmit_power_dbm > 13U ||
        preamble_symbols < 8U ||
        sync_word != 0x12U) {
        return false;
    }
    const auto half_bandwidth = bandwidth_hz / 2U;
    return frequency_hz >= kCertifiedLowerHz + half_bandwidth &&
           frequency_hz <= kCapUpperHz - half_bandwidth;
}

Japan920Policy::Japan920Policy(Japan920PolicyConfig config) noexcept
    : config_(config),
      tokens_ms_(config.bucket_capacity_ms),
      valid_(config.profile.valid() &&
             config.minimum_gap_ms > 0U &&
             config.bucket_capacity_ms > 0U &&
             config.refill_denominator > 0U) {}

TxDecision Japan920Policy::evaluate(
    RadioTick now_ms, std::size_t bytes) noexcept {
    if (!valid_ || bytes == 0U || bytes > kMaximumDatagramBytes) {
        return {TxDecisionStatus::Invalid, 0U};
    }
    if (clock_started_ && now_ms < last_refill_ms_) {
        valid_ = false;
        return {TxDecisionStatus::Locked, 0U};
    }

    refill(now_ms);
    const auto airtime = airtime_ms(bytes);
    if (airtime == 0U || airtime > config_.bucket_capacity_ms) {
        return {TxDecisionStatus::Invalid, 0U};
    }
    if (now_ms < next_transmit_ms_) {
        return {TxDecisionStatus::Deferred, next_transmit_ms_};
    }
    if (tokens_ms_ < airtime) {
        const auto deficit = airtime - tokens_ms_;
        return {TxDecisionStatus::Deferred,
                saturating_add(
                    now_ms,
                    deficit * static_cast<RadioTick>(
                                  config_.refill_denominator))};
    }
    return {TxDecisionStatus::Allowed, now_ms};
}

void Japan920Policy::record(RadioTick now_ms, std::size_t bytes) noexcept {
    if (!valid_) {
        return;
    }
    const auto decision = evaluate(now_ms, bytes);
    if (decision.status != TxDecisionStatus::Allowed) {
        valid_ = false;
        return;
    }
    tokens_ms_ -= airtime_ms(bytes);
    next_transmit_ms_ = saturating_add(now_ms, config_.minimum_gap_ms);
}

bool Japan920Policy::valid() const noexcept {
    return valid_;
}

RadioTick Japan920Policy::airtime_ms(std::size_t bytes) const noexcept {
    if (!config_.profile.valid() || bytes == 0U ||
        bytes > kMaximumDatagramBytes) {
        return 0U;
    }

    const double sf = static_cast<double>(config_.profile.spreading_factor);
    const double symbol_seconds =
        std::ldexp(1.0, config_.profile.spreading_factor) /
        static_cast<double>(config_.profile.bandwidth_hz);
    const double low_data_rate_optimization =
        symbol_seconds >= 0.016 ? 1.0 : 0.0;
    const double numerator =
        8.0 * static_cast<double>(bytes) - 4.0 * sf + 28.0 + 16.0;
    const double denominator =
        4.0 * (sf - 2.0 * low_data_rate_optimization);
    const double coded_groups =
        std::max(0.0, std::ceil(numerator / denominator));
    const double payload_symbols =
        8.0 + coded_groups *
                  static_cast<double>(config_.profile.coding_rate_denominator);
    const double total_symbols =
        static_cast<double>(config_.profile.preamble_symbols) + 4.25 +
        payload_symbols;
    const double milliseconds = total_symbols * symbol_seconds * 1'000.0;
    if (milliseconds <= 0.0 ||
        milliseconds >
            static_cast<double>(std::numeric_limits<RadioTick>::max())) {
        return 0U;
    }
    return static_cast<RadioTick>(std::ceil(milliseconds));
}

void Japan920Policy::refill(RadioTick now_ms) noexcept {
    if (!clock_started_) {
        clock_started_ = true;
        last_refill_ms_ = now_ms;
        return;
    }
    const auto elapsed = now_ms - last_refill_ms_;
    const auto added =
        elapsed / static_cast<RadioTick>(config_.refill_denominator);
    if (added > 0U) {
        tokens_ms_ = std::min(config_.bucket_capacity_ms,
                              saturating_add(tokens_ms_, added));
        last_refill_ms_ = now_ms;
    }
}

} // namespace battleship::net::lora
