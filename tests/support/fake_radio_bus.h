/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
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
    // Cross-radio carrier sense (listen-before-talk): try_start_transmit()
    // returns Busy while another radio's transmission is on air. Mirrors the
    // real SX1262 LBT/CAD. Set false to model hidden terminals (collisions).
    bool carrier_sense = false;
    // Models the SX1262 RxTxFallbackMode STDBY_XOSC: after its emission ends a
    // radio is deaf (in standby) until its next poll() that delivers TxDone
    // (where the driver re-arms RX). Packets that start or end in that window
    // are dropped for that radio. Default off (ideal instant re-arm).
    bool deaf_until_poll = false;
};

class FakeRadioBus;

// Test hook: one entry per transmission that was started on the bus.
struct TxRecord {
    const IRadio* radio = nullptr;
    std::uint64_t start_ms = 0;
    std::vector<std::uint8_t> bytes;
};

// Half duplex: a packet is heard only by radios that were not transmitting
// when it started. Busy comes from set_channel_busy(), the radio's own
// transmission, or (FakeBusConfig::carrier_sense, default off) another radio's
// transmission that is still on air.
class FakeRadio : public IRadio {
public:
    bool ready() const override;
    RadioStart try_start_transmit(const std::uint8_t* data, std::size_t size) override;
    RadioPoll poll(std::uint8_t* dst, std::size_t cap) override;
    void shutdown() override;

    // Test hook modelling Sx1262Radio's latched SPI/GPIO fault: after `n`
    // more poll() calls (0 = the very next one) poll() latches the fault and
    // returns RadioEvent::Failed; from then on ready() is false and every
    // poll() returns Failed again. To keep a caller that loops on Failed from
    // hanging the test run, poll() gives up with Idle after
    // kFaultedPollEscape faulted calls (the test then fails on poll_calls()).
    void latch_fault_after_polls(int n) { fault_countdown_ = n; }
    // Test hook modelling a one-off SX1262 TX timeout: the next poll() aborts
    // any transmission in progress (nothing is delivered) and returns
    // RadioEvent::Failed once; the radio stays ready.
    void fail_next_poll() { fail_once_ = true; }
    std::size_t poll_calls() const { return poll_calls_; }
    static constexpr std::size_t kFaultedPollEscape = 10000;

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
    bool deaf_ = false;       // deaf_until_poll: TxDone ended, RX not re-armed yet
    int tx_done_ = 0;         // TxDone events not yet polled
    int fault_countdown_ = -1; // latch_fault_after_polls(); -1 = never
    bool faulted_ = false;
    bool fail_once_ = false;
    std::size_t poll_calls_ = 0;
    std::size_t faulted_polls_ = 0;
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

    // Test hooks. tx_log() lists every transmission in start order.
    // set_drop_filter(): when it returns true for a transmission's bytes,
    // that whole transmission is delivered to nobody (once per transmission,
    // evaluated at delivery time). Pass nullptr to clear.
    const std::vector<TxRecord>& tx_log() const { return tx_log_; }
    void set_drop_filter(std::function<bool(const std::vector<std::uint8_t>&)> f) { drop_filter_ = std::move(f); }

private:
    friend class FakeRadio;
    bool lost();

    FakeBusConfig cfg_;
    Japan920Policy policy_;
    std::mt19937 rng_;
    std::uint64_t now_ = 0;
    bool busy_ = false;
    std::vector<TxRecord> tx_log_;
    std::function<bool(const std::vector<std::uint8_t>&)> drop_filter_;
    std::vector<std::unique_ptr<FakeRadio>> radios_;
};

} // namespace battleship::net::lora::testing
