/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Pure decision helpers for the BLE connect handshake (no BlueZ, no clock,
 * header-only) so the "link lost / handshake timed out" rules are unit-testable
 * on the desktop build. Used by ble_transport.cpp.
 */
#pragma once

#include <cstdint>

namespace battleship::net {

/// Overall budget for a joiner to reach Connected (TX subscription live),
/// (joiner only).
inline constexpr uint32_t kBleHandshakeTimeoutMs = 15000;

/// True once `timeout_ms` have elapsed since `start_ms` (monotonic ms).
/// Relies on the 64-bit ms counter never wrapping; now < start is treated as
/// "not timed out".
inline bool ble_handshake_timed_out(uint64_t start_ms, uint64_t now_ms,
                                    uint32_t timeout_ms = kBleHandshakeTimeoutMs) {
    return now_ms >= start_ms && now_ms - start_ms >= timeout_ms;
}

/// Interprets a Device1 PropertiesChanged: the link is lost when Connected
/// went false, or ServicesResolved went false after it had been true.
/// `has_*` says whether the signal carried that property at all.
inline bool ble_device_link_lost(bool has_connected, bool connected, bool has_resolved,
                                 bool resolved, bool was_resolved) {
    return (has_connected && !connected) || (has_resolved && !resolved && was_resolved);
}

}  // namespace battleship::net
