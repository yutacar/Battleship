/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Japan 920 MHz LoRa transmit policy (fixed Battleship profile + ARIB STD-T108
 * ch24-32 rules: emission < 4 s, 50 ms pause after each emission, no hourly
 * airtime cap). Ported from LoraMessenger
 * (src/adapters/radio/japan_920_radio_policy.{h,cpp}, MIT, same author),
 * with the profile changed to 921.8 MHz / SF7 / CR 4/5 and the ports::
 * dependency removed. Headless: no LVGL, no hardware access.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace battleship::net::lora {

using RadioTick = std::uint64_t;

// Extra pause added on top of Japan920PolicyConfig::pause_after_emission_ms
// (so 50 + 5 = 55 ms effective). The policy is stamped with a millisecond
// clock read just after the radio accepted SetTx, while the real emission
// starts a little later (SPI transfer, PA ramp) and both clock reads are
// truncated to whole milliseconds. This margin absorbs that SPI/clock
// granularity so the ARIB >= 50 ms pause after the END of our own emission
// holds by construction, not only on average.
constexpr RadioTick kEmissionPauseSafetyMarginMs = 5U;

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
    // ARIB STD-T108 (channels 24-32): pause measured from the END of our own
    // emission before the next one may start. kEmissionPauseSafetyMarginMs is
    // added on top when the next start is scheduled.
    RadioTick pause_after_emission_ms{50U};
};

enum class TxDecisionStatus { Allowed, Deferred, Locked, Invalid };

struct TxDecision {
    TxDecisionStatus status{TxDecisionStatus::Locked};
    RadioTick not_before_ms{0U};
};

// Enforces the per-emission limit (calculated airtime must be < 4 s, enforced
// as <= 400 ms here) and the pause after the previous emission's end.
// Listen-before-talk (carrier sense >= 5 ms) is the transport's responsibility.
// record() must be stamped with a clock read AFTER the radio started the
// emission (not the time evaluate() was called before carrier sense), because
// the next start is scheduled at stamp + airtime + pause + safety margin.
// record() of a decision that is not Allowed is a protocol error and
// invalidates the policy.
class Japan920Policy {
public:
    explicit Japan920Policy(Japan920PolicyConfig config = {}) noexcept;

    TxDecision evaluate(RadioTick now_ms, std::size_t bytes) noexcept;
    void record(RadioTick now_ms, std::size_t bytes) noexcept;

    RadioTick airtime_ms(std::size_t bytes) const noexcept;
    bool valid() const noexcept;

private:
    Japan920PolicyConfig config_;
    RadioTick last_now_ms_{0U};
    RadioTick next_transmit_ms_{0U};
    bool clock_started_{false};
    bool valid_{false};
};

} // namespace battleship::net::lora
