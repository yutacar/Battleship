/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "support/fake_radio_bus.h"

#include <algorithm>
#include <cstring>

namespace battleship::net::lora::testing {

bool FakeRadio::ready() const {
    return ready_;
}

RadioStart FakeRadio::try_start_transmit(const std::uint8_t* data, std::size_t size) {
    if (!ready_) return RadioStart::Failed;
    if (size == 0 || size > kMaxRadioPacket) return RadioStart::Failed;
    if (bus_.busy_ || tx_pending_) return RadioStart::Busy;
    if (bus_.cfg_.carrier_sense) {
        for (const auto& other : bus_.radios_) {
            if (other.get() != this && other->tx_pending_ && other->tx_.end > bus_.now_) {
                return RadioStart::Busy;
            }
        }
    }
    tx_.bytes.assign(data, data + size);
    tx_.start = bus_.now_;
    tx_.end = bus_.now_ + (bus_.cfg_.use_airtime ? bus_.policy_.airtime_ms(size) : 0);
    tx_history_.emplace_back(tx_.start, tx_.end);
    tx_pending_ = true;
    bus_.tx_log_.push_back({this, tx_.start, tx_.bytes});
    return RadioStart::Started;
}

RadioPoll FakeRadio::poll(std::uint8_t* dst, std::size_t cap) {
    RadioPoll r;
    if (!ready_) return r;
    if (tx_done_ > 0) {
        --tx_done_;
        if (tx_done_ == 0 && deaf_) {
            // RX re-armed now: the deaf window (emission end -> this poll)
            // also covers packets that started inside it but end after it.
            deaf_ = false;
            if (!tx_history_.empty()) tx_history_.back().second = std::max(tx_history_.back().second, bus_.now_);
        }
        r.event = RadioEvent::TxDone;
        return r;
    }
    if (!rx_.empty()) {
        Rx rx = std::move(rx_.front());
        rx_.pop_front();
        const std::size_t n = std::min(rx.bytes.size(), cap);
        if (n > 0) std::memcpy(dst, rx.bytes.data(), n);
        r.event = RadioEvent::Received;
        r.size = n;
        r.rssi_dbm = rx.rssi;
    }
    return r;
}

void FakeRadio::shutdown() {
    ready_ = false;
    tx_pending_ = false;
    deaf_ = false;
    tx_done_ = 0;
    rx_.clear();
}

FakeRadioBus::FakeRadioBus(FakeBusConfig cfg) : cfg_(cfg), rng_(cfg.seed) {}

FakeRadio& FakeRadioBus::add_radio(std::int16_t rssi_dbm) {
    radios_.push_back(std::unique_ptr<FakeRadio>(new FakeRadio(*this, rssi_dbm)));
    return *radios_.back();
}

bool FakeRadioBus::lost() {
    if (cfg_.loss_rate <= 0) return false;
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < cfg_.loss_rate;
}

void FakeRadioBus::advance_to(std::uint64_t now_ms) {
    if (now_ms > now_) now_ = now_ms;
    // Finish transmissions in end-time order so delivery order is causal.
    for (;;) {
        FakeRadio* next = nullptr;
        for (auto& r : radios_) {
            if (r->tx_pending_ && r->tx_.end <= now_ && (!next || r->tx_.end < next->tx_.end)) {
                next = r.get();
            }
        }
        if (!next) break;
        next->tx_pending_ = false;
        ++next->tx_done_;
        if (cfg_.deaf_until_poll) next->deaf_ = true;
        if (drop_filter_ && drop_filter_(next->tx_.bytes)) continue;
        const std::uint64_t start = next->tx_.start;
        for (auto& rx : radios_) {
            if (rx.get() == next || !rx->ready_ || rx->deaf_) continue;
            const bool deaf = std::any_of(rx->tx_history_.begin(), rx->tx_history_.end(),
                                          [&](const auto& iv) { return iv.first <= start && start < iv.second; });
            if (deaf || lost()) continue;
            rx->rx_.push_back({next->tx_.bytes, rx->rssi_});
        }
    }
}

void FakeRadioBus::inject_foreign(const std::vector<std::uint8_t>& bytes) {
    for (auto& r : radios_) {
        if (r->ready_) r->rx_.push_back({bytes, r->rssi_});
    }
}

} // namespace battleship::net::lora::testing
