/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace platform {

/// Read-only Wi-Fi association probe (never changes system settings).
/// True when any interface under `sysfs_net_root` that has a `wireless`
/// subdirectory (or is named wlan*) reports operstate exactly "up" (trailing
/// newline ignored). "dormant"/"down"/unknown, and missing files or
/// directories, yield false. Never throws.
///
/// On the CardputerZero the BT/Wi-Fi combo chip shares one antenna, so BLE
/// matches fail while Wi-Fi is associated; the lobby/join screens use this to
/// warn up front. The default root does not exist on macOS (-> false).
bool wifi_connected(const std::string& sysfs_net_root = "/sys/class/net");

}  // namespace platform
