/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace battleship::net::lora {

// Largest LoRa payload the Battleship protocol ever sends.
constexpr std::size_t kMaxRadioPacket = 80;

enum class RadioStart { Started, Busy, Failed }; // Busy: carrier sense hit or receiving
enum class RadioEvent { Idle, TxDone, Received, Failed };

struct RadioPoll {
    RadioEvent event = RadioEvent::Idle;
    std::size_t size = 0;
    std::int16_t rssi_dbm = 0;
    std::int8_t snr_quarter_db = 0; // packet SNR in 0.25 dB units (diagnostics)
};

enum class RadioInitError {
    None,
    PowerFailed,
    SpiOpenFailed,
    GpioFailed,
    ExpanderMissing,
    RadioNoResponse,
    ConfigFailed
};

// Contract: try_start_transmit includes listen-before-talk. When a
// transmission finishes, poll() returns TxDone exactly once and the radio
// returns to receive mode on its own. Reception is always on otherwise.
class IRadio {
public:
    virtual ~IRadio() = default;
    virtual bool ready() const = 0;
    virtual RadioStart try_start_transmit(const std::uint8_t* data, std::size_t size) = 0;
    virtual RadioPoll poll(std::uint8_t* dst, std::size_t cap) = 0;
    virtual void shutdown() = 0;
};

} // namespace battleship::net::lora
