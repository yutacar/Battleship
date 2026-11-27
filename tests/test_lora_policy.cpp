/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/japan920_policy.h"

#include <gtest/gtest.h>

using namespace battleship::net::lora;

TEST(LoraPolicy, DefaultProfileIsBattleshipProfile) {
    Japan920Profile p;
    EXPECT_EQ(p.frequency_hz, 921'800'000U);
    EXPECT_EQ(p.bandwidth_hz, 125'000U);
    EXPECT_EQ(p.spreading_factor, 7U);
    EXPECT_EQ(p.coding_rate_denominator, 5U);
    EXPECT_EQ(p.transmit_power_dbm, 13U);
    EXPECT_EQ(p.preamble_symbols, 8U);
    EXPECT_EQ(p.sync_word, 0x12U);
    EXPECT_TRUE(p.valid());
    EXPECT_TRUE(Japan920Policy{}.valid());
}

TEST(LoraPolicy, RejectsIllegalProfiles) {
    Japan920Profile p;
    p.frequency_hz = 868'000'000U;
    EXPECT_FALSE(p.valid());
    p = {};
    p.transmit_power_dbm = 14U;
    EXPECT_FALSE(p.valid());
    p = {};
    p.bandwidth_hz = 250'000U;
    EXPECT_FALSE(p.valid());
    p = {};
    p.frequency_hz = 923'000'000U;
    EXPECT_FALSE(p.valid());
    p = {};
    p.sync_word = 0x34U;
    EXPECT_FALSE(p.valid());
}

TEST(LoraPolicy, AirtimeSf7) {
    Japan920Policy policy;
    EXPECT_GE(policy.airtime_ms(18), 41U);
    EXPECT_LE(policy.airtime_ms(18), 52U);
    EXPECT_GE(policy.airtime_ms(65), 110U);
    EXPECT_LE(policy.airtime_ms(65), 135U);
}

TEST(LoraPolicy, MinimumGapDefers) {
    Japan920Policy policy;
    ASSERT_EQ(policy.evaluate(0, 18).status, TxDecisionStatus::Allowed);
    policy.record(0, 18);
    const auto d = policy.evaluate(50, 18);
    EXPECT_EQ(d.status, TxDecisionStatus::Deferred);
    EXPECT_EQ(d.not_before_ms, 100U);
}

TEST(LoraPolicy, BucketLimitsLongTermDuty) {
    Japan920Policy policy;
    RadioTick t = 0;
    bool deferred = false;
    for (int i = 0; i < 300 && !deferred; ++i, t += 100) {
        if (policy.evaluate(t, 18).status == TxDecisionStatus::Allowed) {
            policy.record(t, 18);
        } else {
            deferred = true;
        }
    }
    ASSERT_TRUE(deferred);

    RadioTick recorded = 0;
    const RadioTick end = t + 60'000;
    for (; t < end; t += 100) {
        if (policy.evaluate(t, 18).status == TxDecisionStatus::Allowed) {
            policy.record(t, 18);
            recorded += policy.airtime_ms(18);
        }
    }
    EXPECT_LE(recorded, 60'000U / 10U + policy.airtime_ms(18));
}
