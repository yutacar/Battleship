/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/sx1262_math.h"

#include <gtest/gtest.h>

using namespace battleship::net::lora;

TEST(Sx1262Math, RssiConversion) {
    EXPECT_EQ(sx1262_rssi_dbm(0), 0);
    EXPECT_EQ(sx1262_rssi_dbm(180), -90);
    EXPECT_EQ(sx1262_rssi_dbm(181), -90);
    EXPECT_EQ(sx1262_rssi_dbm(182), -91);
    EXPECT_EQ(sx1262_rssi_dbm(255), -127);
}

TEST(Sx1262Math, CarrierBusyAtOrAboveThreshold) {
    EXPECT_TRUE(sx1262_carrier_busy(179, -90));  // -89
    EXPECT_TRUE(sx1262_carrier_busy(180, -90));  // -90, equal => busy
    EXPECT_FALSE(sx1262_carrier_busy(182, -90)); // -91
    EXPECT_FALSE(sx1262_carrier_busy(255, -90));
}

TEST(Sx1262Math, SyncWordBytes) {
    EXPECT_EQ(sx1262_sync_word_bytes(0x12), (std::array<std::uint8_t, 2>{0x14, 0x24}));
    EXPECT_EQ(sx1262_sync_word_bytes(0x34), (std::array<std::uint8_t, 2>{0x34, 0x44}));
}

TEST(Sx1262Math, FrequencyWord) {
    // 921.8 MHz * 2^25 / 32 MHz
    EXPECT_EQ(sx1262_frequency_word(921'800'000U), 966577356U);
    EXPECT_EQ(sx1262_frequency_word(915'000'000U), 959'447'040U);
}

TEST(Sx1262Math, LbtConfigLimits) {
    EXPECT_TRUE(sx1262_lbt_config_valid(5, -90));
    EXPECT_TRUE(sx1262_lbt_config_valid(5, -80));
    EXPECT_FALSE(sx1262_lbt_config_valid(4, -90));
    EXPECT_FALSE(sx1262_lbt_config_valid(5, -79));
    EXPECT_FALSE(sx1262_lbt_config_valid(0, -90));
}
