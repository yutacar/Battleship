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
    p.frequency_hz = 922'400'000U;
    EXPECT_FALSE(p.valid());
    p = {};
    p.frequency_hz = 920'500'000U;
    EXPECT_FALSE(p.valid());
    p = {};
    p.frequency_hz = 920'600'000U;
    EXPECT_TRUE(p.valid());
    p.frequency_hz = 922'200'000U;
    EXPECT_TRUE(p.valid());
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

TEST(LoraPolicy, PauseIsMeasuredFromEmissionEnd) {
    Japan920Policy policy;
    const RadioTick air = policy.airtime_ms(65);
    ASSERT_EQ(policy.evaluate(0, 65).status, TxDecisionStatus::Allowed);
    policy.record(0, 65);
    const auto early = policy.evaluate(air + 49, 18);
    EXPECT_EQ(early.status, TxDecisionStatus::Deferred);
    EXPECT_EQ(early.not_before_ms, air + 50);
    EXPECT_EQ(policy.evaluate(air + 50, 18).status, TxDecisionStatus::Allowed);
}

TEST(LoraPolicy, NoHourlyCapOnCh24to32) {
    Japan920Policy policy;
    const RadioTick air = policy.airtime_ms(18);
    RadioTick t = 0;
    RadioTick total = 0;
    while (t < 3'600'000U) {
        const auto d = policy.evaluate(t, 18);
        ASSERT_EQ(d.status, TxDecisionStatus::Allowed) << "t=" << t;
        policy.record(t, 18);
        total += air;
        const auto next = policy.evaluate(t, 18);
        ASSERT_EQ(next.status, TxDecisionStatus::Deferred);
        ASSERT_EQ(next.not_before_ms, t + air + 50);
        t = next.not_before_ms;
    }
    EXPECT_GT(total, 360'000U);
}

TEST(LoraPolicy, AirtimeOfLargestPacketBelowLimit) {
    Japan920Policy policy;
    EXPECT_LE(policy.airtime_ms(80), 400U);
    // Even the 255-byte maximum is exactly 400 ms: allowed (rule is > 400 Invalid).
    EXPECT_EQ(policy.airtime_ms(255), 400U);
    EXPECT_EQ(policy.evaluate(0, 255).status, TxDecisionStatus::Allowed);
    EXPECT_EQ(policy.evaluate(0, 0).status, TxDecisionStatus::Invalid);
    EXPECT_EQ(policy.evaluate(0, 256).status, TxDecisionStatus::Invalid);
}

TEST(LoraPolicy, ClockRegressionLocks) {
    Japan920Policy policy;
    ASSERT_EQ(policy.evaluate(1000, 18).status, TxDecisionStatus::Allowed);
    EXPECT_EQ(policy.evaluate(999, 18).status, TxDecisionStatus::Locked);
}
