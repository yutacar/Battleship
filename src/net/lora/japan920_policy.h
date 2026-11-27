/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Japan 920 MHz LoRa transmit policy (fixed Battleship profile + token-bucket
 * airtime budget). Ported from LoraMessenger
 * (src/adapters/radio/japan_920_radio_policy.{h,cpp}, MIT, same author),
 * with the profile changed to 921.8 MHz / SF7 / CR 4/5 and the ports::
 * dependency removed. Headless: no LVGL, no hardware access.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace battleship::net::lora {

using RadioTick = std::uint64_t;

struct Japan920Profile {
    std::uint32_t frequency_hz{921'800'000U};
    std::uint32_t bandwidth_hz{125'000U};
    std::uint8_t spreading_factor{7U};
    std::uint8_t coding_rate_denominator{5U};
    std::uint8_t transmit_power_dbm{13U};
    std::uint16_t preamble_symbols{8U};
    std::uint8_t sync_word{0x12U};

    bool valid() const noexcept;
};

struct Japan920PolicyConfig {
    Japan920Profile profile;
    RadioTick minimum_gap_ms{100U};
    RadioTick bucket_capacity_ms{6'000U};
    std::uint32_t refill_denominator{10U};
};

enum class TxDecisionStatus { Allowed, Deferred, Locked, Invalid };

struct TxDecision {
    TxDecisionStatus status{TxDecisionStatus::Locked};
    RadioTick not_before_ms{0U};
};

// Conservative congestion budget: the token bucket permits at most 10%
// long-term calculated airtime by default. Listen-before-talk is still the
// transport's responsibility.
class Japan920Policy {
public:
    explicit Japan920Policy(Japan920PolicyConfig config = {}) noexcept;

    TxDecision evaluate(RadioTick now_ms, std::size_t bytes) noexcept;
    void record(RadioTick now_ms, std::size_t bytes) noexcept;

    RadioTick airtime_ms(std::size_t bytes) const noexcept;
    bool valid() const noexcept;

private:
    void refill(RadioTick now_ms) noexcept;

    Japan920PolicyConfig config_;
    RadioTick tokens_ms_{0U};
    RadioTick last_refill_ms_{0U};
    RadioTick next_transmit_ms_{0U};
    bool clock_started_{false};
    bool valid_{false};
};

} // namespace battleship::net::lora
