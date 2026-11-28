/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Simulated LoRa radio over UDP multicast (239.255.53.60) for desktop
 * development. Every datagram carries a 4-byte instance id so an instance
 * ignores its own looped-back packets. Real-time (steady_clock) airtime from
 * Japan920Policy delays TxDone. Carrier sense is local only: Busy while this
 * instance has not yet reported TxDone. poll() never blocks.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>

#include "net/lora/japan920_policy.h"
#include "net/lora/radio_iface.h"

namespace battleship::net::lora {

class UdpSimRadio final : public IRadio {
public:
    explicit UdpSimRadio(double loss_rate = 0.0, std::uint16_t port = 35360);
    ~UdpSimRadio() override;

    UdpSimRadio(const UdpSimRadio&) = delete;
    UdpSimRadio& operator=(const UdpSimRadio&) = delete;

    bool ready() const override { return fd_ >= 0; }
    RadioStart try_start_transmit(const std::uint8_t* data, std::size_t size) override;
    RadioPoll poll(std::uint8_t* dst, std::size_t cap) override;
    void shutdown() override;

private:
    using Clock = std::chrono::steady_clock;

    int fd_ = -1;
    std::uint16_t port_;
    double loss_rate_;
    std::uint32_t instance_id_;
    Japan920Policy policy_;
    bool tx_active_ = false;
    Clock::time_point tx_done_at_{};
    std::mt19937 rng_;
};

} // namespace battleship::net::lora
