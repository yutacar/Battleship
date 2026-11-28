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
