/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * This machine's short host name, used to name the BLE advertisement and the
 * LoRa beacon of a hosted match.
 */

#pragma once

#include <string>

namespace battleship::net {

/// This machine's hostname (short form), or "battleship" as fallback.
std::string local_host_name();

}  // namespace battleship::net
