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
// ARIB limit is < 4 s per emission; this project keeps a 10x margin.
constexpr RadioTick kMaximumAirtimeMs = 400U;

RadioTick saturating_add(RadioTick left, RadioTick right) noexcept {
    const auto maximum = std::numeric_limits<RadioTick>::max();
    return left > maximum - right ? maximum : left + right;
}

} // namespace

bool Japan920Profile::valid() const noexcept {
    // ARIB STD-T108 channels 24-32 (center 920.6-922.2 MHz): 20 mW,
    // 5 ms carrier sense, < 4 s emission, 50 ms pause, no hourly cap.
    constexpr std::uint32_t kLowerCenterHz = 920'600'000U;
    constexpr std::uint32_t kUpperCenterHz = 922'200'000U;
    if (bandwidth_hz != 125'000U ||
        spreading_factor < 7U || spreading_factor > 12U ||
        coding_rate_denominator < 5U || coding_rate_denominator > 8U ||
        transmit_power_dbm > 13U ||
        preamble_symbols < 8U ||
        sync_word != 0x12U) {
        return false;
    }
    return frequency_hz >= kLowerCenterHz && frequency_hz <= kUpperCenterHz;
}

Japan920Policy::Japan920Policy(Japan920PolicyConfig config) noexcept
    : config_(config), valid_(config.profile.valid()) {}

TxDecision Japan920Policy::evaluate(
    RadioTick now_ms, std::size_t bytes) noexcept {
    if (!valid_ || bytes == 0U || bytes > kMaximumDatagramBytes) {
        return {TxDecisionStatus::Invalid, 0U};
    }
    if (clock_started_ && now_ms < last_now_ms_) {
        valid_ = false;
        return {TxDecisionStatus::Locked, 0U};
    }
    clock_started_ = true;
    last_now_ms_ = now_ms;

    const auto airtime = airtime_ms(bytes);
    if (airtime == 0U || airtime > kMaximumAirtimeMs) {
        return {TxDecisionStatus::Invalid, 0U};
    }
    if (now_ms < next_transmit_ms_) {
        return {TxDecisionStatus::Deferred, next_transmit_ms_};
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
    next_transmit_ms_ = saturating_add(
        saturating_add(saturating_add(now_ms, airtime_ms(bytes)), config_.pause_after_emission_ms),
        kEmissionPauseSafetyMarginMs);
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

} // namespace battleship::net::lora
