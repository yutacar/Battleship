/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "net/ble/ble_handshake_policy.h"

using namespace battleship::net;

TEST(BleHandshakePolicy, TimeoutBoundary) {
    EXPECT_FALSE(ble_handshake_timed_out(1000, 1000));
    EXPECT_FALSE(ble_handshake_timed_out(1000, 1000 + kBleHandshakeTimeoutMs - 1));
    EXPECT_TRUE(ble_handshake_timed_out(1000, 1000 + kBleHandshakeTimeoutMs));
    EXPECT_TRUE(ble_handshake_timed_out(0, 20000, 100));
    EXPECT_FALSE(ble_handshake_timed_out(5000, 4000));  // clock never goes backwards -> no spurious fail
}

TEST(BleHandshakePolicy, ConnectedFalseIsLost) {
    EXPECT_TRUE(ble_device_link_lost(true, false, false, false, false));
    EXPECT_TRUE(ble_device_link_lost(true, false, true, false, true));
    EXPECT_FALSE(ble_device_link_lost(true, true, false, false, false));
}

TEST(BleHandshakePolicy, ResolvedFalseOnlyLostAfterTrue) {
    EXPECT_TRUE(ble_device_link_lost(false, false, true, false, true));
    EXPECT_FALSE(ble_device_link_lost(false, false, true, false, false));  // not yet resolved
    EXPECT_FALSE(ble_device_link_lost(false, false, true, true, true));
    EXPECT_FALSE(ble_device_link_lost(false, false, false, false, true));  // unrelated property
}

TEST(BleHandshakePolicy, RetryAttemptsBudget) {
    EXPECT_EQ(kBleConnectAttempts, 3);
    EXPECT_TRUE(ble_should_retry(1));
    EXPECT_TRUE(ble_should_retry(2));
    EXPECT_FALSE(ble_should_retry(3));
    EXPECT_FALSE(ble_should_retry(4));
}

TEST(BleHandshakePolicy, RetryDelayElapsed) {
    EXPECT_EQ(kBleRetryDelayMs, 1000u);
    EXPECT_FALSE(ble_retry_delay_elapsed(5000, 5000));
    EXPECT_FALSE(ble_retry_delay_elapsed(5000, 5000 + kBleRetryDelayMs - 1));
    EXPECT_TRUE(ble_retry_delay_elapsed(5000, 5000 + kBleRetryDelayMs));
    EXPECT_FALSE(ble_retry_delay_elapsed(5000, 4000));  // clock never goes backwards
}

TEST(BleHandshakePolicy, RetryReadyGatesOnTeardownReply) {
    EXPECT_FALSE(ble_retry_ready(5500, 5000, false));                       // delay not elapsed
    EXPECT_TRUE(ble_retry_ready(6000, 5000, false));                        // nothing outstanding
    EXPECT_FALSE(ble_retry_ready(6000, 5000, true));                        // Disconnect reply pending
    EXPECT_FALSE(ble_retry_ready(5000 + kBleTeardownWaitMs - 1, 5000, true));
    EXPECT_TRUE(ble_retry_ready(5000 + kBleTeardownWaitMs, 5000, true));    // bound reached
}
