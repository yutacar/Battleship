/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Ownership of the Cap LoRa-1262 5V output (ext_5v_out LED node). The
 * CardputerZero may already have 5V on for another user, so power is only
 * switched off again by the process that switched it on. A marker file in
 * the runtime directory records that ownership across restarts.
 */

#pragma once

#include <cstdint>
#include <string>

namespace battleship::net::lora {

inline constexpr char kCapPowerPath[] = "/sys/class/leds/ext_5v_out/brightness";
inline constexpr char kCapPowerMarker[] = "battleship-cap-power";

// $XDG_RUNTIME_DIR when set and non-empty, otherwise "/tmp".
std::string default_marker_dir();

class CapPower {
public:
    CapPower(std::string brightness_path = kCapPowerPath,
             std::string marker_dir = default_marker_dir(),
             std::uint32_t settle_ms = 100);
    ~CapPower();

    CapPower(const CapPower&) = delete;
    CapPower& operator=(const CapPower&) = delete;

    // Returns false only when the brightness file cannot be read or (when
    // turning power on) written. If power is already on and not marked as
    // ours, it is left alone and the call still succeeds, but owned() is false.
    bool acquire();

    // Turns power off and removes the marker, only when owned().
    void release();

    bool owned() const { return owned_; }

private:
    std::string brightness_path_;
    std::string marker_path_;
    std::uint32_t settle_ms_;
    bool owned_ = false;
};

}  // namespace battleship::net::lora
