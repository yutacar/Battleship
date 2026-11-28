/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_state.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace platform {

bool wifi_connected(const std::string& sysfs_net_root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(sysfs_net_root, ec);
    if (ec) {
        return false;
    }
    const fs::directory_iterator end;
    while (it != end) {
        const fs::path dir = it->path();
        const std::string name = dir.filename().string();
        std::error_code probe_ec;
        const bool wireless = name.rfind("wlan", 0) == 0 || fs::is_directory(dir / "wireless", probe_ec);
        if (wireless) {
            std::ifstream in(dir / "operstate");
            std::string state;
            if (in && std::getline(in, state)) {
                while (!state.empty() && (state.back() == '\r' || state.back() == '\n')) {
                    state.pop_back();
                }
                if (state == "up") {
                    return true;
                }
            }
        }
        it.increment(ec);
        if (ec) {
            break;
        }
    }
    return false;
}

}  // namespace platform
