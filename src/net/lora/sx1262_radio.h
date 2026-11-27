/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * SX1262 driver for the Cap LoRa-1262 on CardputerZero (Linux only: spidev,
 * GPIO chardev, i2c-dev). Ported from LoraMessenger
 * (src/adapters/radio/linux_cap_lora_1262_radio.{h,cpp}, MIT, same author).
 * Cap 5V power must already be on (see CapPower). Transmit-time pauses are
 * enforced by LoRaLink/Japan920Policy; this driver only does carrier sense.
 */

#pragma once

#include "net/lora/japan920_policy.h"
#include "net/lora/radio_iface.h"

#include <cstdint>
#include <memory>
#include <string>

namespace battleship::net::lora {

struct Sx1262Config {
    // CardputerZero EXT uses SPI0 CS1 for the Zero-compatible Cap orientation.
    std::string spi_device = "/dev/spidev0.1";
    std::string gpio_chip = "/dev/gpiochip0";
    std::string i2c_device = "/dev/i2c-1";
    std::uint32_t reset_gpio = 26;
    std::uint32_t irq_gpio = 23;
    std::uint32_t busy_gpio = 22;
    std::uint32_t spi_speed_hz = 4'000'000;
    std::int16_t lbt_threshold_dbm = -90;
    std::uint32_t carrier_sense_ms = 5;
    Japan920Profile profile;

    bool valid() const noexcept;
};

class Sx1262Radio final : public IRadio {
public:
    explicit Sx1262Radio(Sx1262Config config) noexcept;
    ~Sx1262Radio() override;

    Sx1262Radio(const Sx1262Radio&) = delete;
    Sx1262Radio& operator=(const Sx1262Radio&) = delete;

    RadioInitError init_error() const noexcept;

    bool ready() const override;
    RadioStart try_start_transmit(const std::uint8_t* data, std::size_t size) override;
    RadioPoll poll(std::uint8_t* dst, std::size_t cap) override;
    void shutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battleship::net::lora
