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

/// Budget PER ATTEMPT for a joiner to reach Connected (TX subscription live),
/// (joiner only).
inline constexpr uint32_t kBleHandshakeTimeoutMs = 15000;

/// Total Device1.Connect attempts a joiner makes before giving up (the combo
/// chip's shared WiFi/BT antenna makes the first LE links drop often).
inline constexpr int kBleConnectAttempts = 3;

/// Pause before each retry.
inline constexpr uint32_t kBleRetryDelayMs = 1000;

/// True when another attempt may be made after `attempts_made` (>= 1) failed.
inline bool ble_should_retry(int attempts_made) {
    return attempts_made < kBleConnectAttempts;
}

/// True once the retry pause has elapsed since the failure at `failed_ms`.
inline bool ble_retry_delay_elapsed(uint64_t failed_ms, uint64_t now_ms,
                                    uint32_t delay_ms = kBleRetryDelayMs) {
    return now_ms >= failed_ms && now_ms - failed_ms >= delay_ms;
}

/// True once `timeout_ms` have elapsed since `start_ms` (monotonic ms).
/// Relies on the 64-bit ms counter never wrapping; now < start is treated as
/// "not timed out".
inline bool ble_handshake_timed_out(uint64_t start_ms, uint64_t now_ms,
                                    uint32_t timeout_ms = kBleHandshakeTimeoutMs) {
    return now_ms >= start_ms && now_ms - start_ms >= timeout_ms;
}

/// Longest a retry waits for the previous attempt's teardown Disconnect (or a
/// still-pending Connect) reply before going ahead anyway.
inline constexpr uint32_t kBleTeardownWaitMs = 5000;

/// True when the next Connect may be issued: the retry pause has elapsed AND
/// either no teardown reply is outstanding or the wait bound has passed
/// (reissuing while the old link is still being torn down would let BlueZ's
/// delayed ACL drop kill the new attempt).
inline bool ble_retry_ready(uint64_t now_ms, uint64_t failed_ms, bool waiting_for_reply,
                            uint32_t delay_ms = kBleRetryDelayMs,
                            uint32_t wait_bound_ms = kBleTeardownWaitMs) {
    if (!ble_retry_delay_elapsed(failed_ms, now_ms, delay_ms)) return false;
    return !waiting_for_reply || ble_retry_delay_elapsed(failed_ms, now_ms, wait_bound_ms);
}

/// Interprets a Device1 PropertiesChanged: the link is lost when Connected
/// went false, or ServicesResolved went false after it had been true.
/// `has_*` says whether the signal carried that property at all.
inline bool ble_device_link_lost(bool has_connected, bool connected, bool has_resolved,
                                 bool resolved, bool was_resolved) {
    return (has_connected && !connected) || (has_resolved && !resolved && was_resolved);
}

}  // namespace battleship::net
