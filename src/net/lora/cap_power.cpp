/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/cap_power.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>
#include <utility>

namespace battleship::net::lora {

namespace fs = std::filesystem;

namespace {

// Reads the first character of the brightness file. Returns '\0' on failure.
char read_state(const std::string& path) {
    std::ifstream in(path);
    char c = '\0';
    if (!in.get(c)) return '\0';
    return c;
}

bool write_state(const std::string& path, char value) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << value << '\n';
    out.flush();
    return static_cast<bool>(out);
}

}  // namespace

std::string default_marker_dir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && *xdg != '\0') return xdg;
    return "/tmp";
}

CapPower::CapPower(std::string brightness_path, std::string marker_dir,
                   std::uint32_t settle_ms)
    : brightness_path_(std::move(brightness_path)),
      marker_path_((fs::path(marker_dir) / kCapPowerMarker).string()),
      settle_ms_(settle_ms) {}

CapPower::~CapPower() { release(); }

bool CapPower::acquire() {
    const char state = read_state(brightness_path_);
    if (state == '\0') return false;

    if (state == '0') {
        if (!write_state(brightness_path_, '1')) return false;
        {
            std::ofstream marker(marker_path_, std::ios::trunc);
            if (!marker) {
                write_state(brightness_path_, '0');
                return false;
            }
        }
        owned_ = true;
        if (settle_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms_));
        }
        return true;
    }

    // Power already on. A marker means it is left over from our own earlier
    // run, so it is ours to switch off. Without a marker it belongs to someone
    // else and is not touched.
    owned_ = fs::exists(marker_path_);
    return true;
}

void CapPower::release() {
    if (!owned_) return;
    // If the power-off write fails, keep the marker and ownership so a later
    // release() (or the destructor) can retry. The marker is the only record
    // that the power is ours across restarts.
    if (!write_state(brightness_path_, '0')) return;
    std::error_code ec;
    fs::remove(marker_path_, ec);
    owned_ = false;
}

}  // namespace battleship::net::lora
