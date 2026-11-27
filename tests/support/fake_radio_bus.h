/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "net/lora/japan920_policy.h"
#include "net/lora/radio_iface.h"

namespace battleship::net::lora::testing {

struct FakeBusConfig {
    double loss_rate = 0;
    std::uint32_t seed = 1;
    bool use_airtime = true;
};

class FakeRadioBus;

// Half duplex: a packet is heard only by radios that were not transmitting
// when it started. There is no cross-radio carrier sense (hidden terminals);
// Busy comes only from set_channel_busy() or the radio's own transmission.
class FakeRadio : public IRadio {
public:
    bool ready() const override;
    RadioStart try_start_transmit(const std::uint8_t* data, std::size_t size) override;
    RadioPoll poll(std::uint8_t* dst, std::size_t cap) override;
    void shutdown() override;

private:
    friend class FakeRadioBus;
    FakeRadio(FakeRadioBus& bus, std::int16_t rssi) : bus_(bus), rssi_(rssi) {}

    struct Tx {
        std::vector<std::uint8_t> bytes;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
    };
    struct Rx {
        std::vector<std::uint8_t> bytes;
        std::int16_t rssi = 0;
    };

    FakeRadioBus& bus_;
    std::int16_t rssi_;
    bool ready_ = true;
    bool tx_pending_ = false; // transmitting, delivery not yet done
    bool tx_done_ = false;    // TxDone not yet polled
    Tx tx_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> tx_history_;
    std::deque<Rx> rx_;
};

class FakeRadioBus {
public:
    explicit FakeRadioBus(FakeBusConfig cfg = {});

    FakeRadio& add_radio(std::int16_t rssi_dbm = -60);
    void advance_to(std::uint64_t now_ms);
    // Deliver bytes from a non-Battleship transmitter to every radio now.
    void inject_foreign(const std::vector<std::uint8_t>& bytes);
    void set_channel_busy(bool busy) { busy_ = busy; }

private:
    friend class FakeRadio;
    bool lost();

    FakeBusConfig cfg_;
    Japan920Policy policy_;
    std::mt19937 rng_;
    std::uint64_t now_ = 0;
    bool busy_ = false;
    std::vector<std::unique_ptr<FakeRadio>> radios_;
};

} // namespace battleship::net::lora::testing
