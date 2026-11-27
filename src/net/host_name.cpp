/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/host_name.h"

#include <unistd.h>

namespace battleship::net {

std::string local_host_name() {
    char name[128] = {};
    if (gethostname(name, sizeof(name) - 1) != 0 || name[0] == '\0') {
        return "battleship";
    }
    // Short form: strip everything after the first dot (mDNS suffixes etc.).
    std::string result(name);
    const auto dot = result.find('.');
    if (dot != std::string::npos) result.resize(dot);
    return result;
}

}  // namespace battleship::net
