/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/lora_link.h"

#include <algorithm>

namespace battleship::net::lora {

namespace {
bool is_control(PacketType t) {
    return t != PacketType::Data;
}
} // namespace

LoRaLink::LoRaLink(IRadio& radio, Japan920Policy policy, std::uint32_t seed, LinkTiming timing)
    : radio_(radio), policy_(policy), rng_(seed), timing_(timing) {}

void LoRaLink::start_host(const std::string& host_name, std::uint64_t now_ms) {
    host_name_ = host_name.substr(0, kMaxHostNameLen);
    session_id_ = static_cast<std::uint16_t>(1 + rng_() % 0xFFFE);
    tx_queue_.clear();
    peers_.clear();
    rx_data_.clear();
    state_ = LinkState::Beaconing;
    next_beacon_ms_ = now_ms;
}

void LoRaLink::start_scan(std::uint64_t) {
    tx_queue_.clear();
    peers_.clear();
    rx_data_.clear();
    session_id_ = 0;
    state_ = LinkState::Scanning;
}

bool LoRaLink::join(std::uint16_t session_id, std::uint64_t now_ms) {
    if (state_ != LinkState::Idle && state_ != LinkState::Scanning) return false;
    session_id_ = session_id;
    nonce_ = static_cast<std::uint16_t>(rng_());
    state_ = LinkState::Joining;
    join_attempts_ = 0;
    next_join_ms_ = now_ms; // first JOIN goes out on this poll
    Packet p;
    p.type = PacketType::Join;
    p.session_id = session_id_;
    p.nonce = nonce_;
    enqueue(p);
    ++join_attempts_;
    next_join_ms_ = now_ms + timing_.join_retry_ms;
    return true;
}

bool LoRaLink::send_data(const std::uint8_t* data, std::size_t len) {
    if (state_ != LinkState::Connected || !data || len == 0 || len > kMaxDataLen) return false;
    Packet p;
    p.type = PacketType::Data;
    p.session_id = session_id_;
    p.data.assign(data, data + len);
    enqueue(p);
    return true;
}

std::size_t LoRaLink::take_received(std::vector<std::uint8_t>& out) {
    const std::size_t n = rx_data_.size();
    out.insert(out.end(), rx_data_.begin(), rx_data_.end());
    rx_data_.clear();
    return n;
}

void LoRaLink::close(std::uint64_t) {
    if (state_ == LinkState::Connected) {
        // Pending DATA is pointless now; keep only control packets.
        drop_queued(PacketType::Data);
        Packet p;
        p.type = PacketType::Bye;
        p.session_id = session_id_;
        enqueue(p);
    } else {
        tx_queue_.clear();
    }
    state_ = LinkState::Closed;
}

bool LoRaLink::queued(PacketType t) const {
    return std::any_of(tx_queue_.begin(), tx_queue_.end(), [t](const Entry& e) { return e.type == t; });
}

void LoRaLink::drop_queued(PacketType t) {
    tx_queue_.erase(std::remove_if(tx_queue_.begin(), tx_queue_.end(), [t](const Entry& e) { return e.type == t; }),
                    tx_queue_.end());
}

void LoRaLink::enqueue(const Packet& p) {
    Entry e{encode_packet(p), p.type};
    if (e.bytes.empty()) return;
    if (is_control(p.type)) {
        // Control packets go ahead of DATA, FIFO among themselves.
        auto it = std::find_if(tx_queue_.begin(), tx_queue_.end(), [](const Entry& q) { return !is_control(q.type); });
        tx_queue_.insert(it, std::move(e));
    } else {
        tx_queue_.push_back(std::move(e));
    }
    while (tx_queue_.size() > timing_.tx_queue_limit) {
        auto it = std::find_if(tx_queue_.begin(), tx_queue_.end(), [](const Entry& q) { return !is_control(q.type); });
        tx_queue_.erase(it == tx_queue_.end() ? tx_queue_.begin() : it);
    }
}

void LoRaLink::handle(const Packet& p, std::int16_t rssi, std::uint64_t now_ms) {
    switch (p.type) {
    case PacketType::Beacon:
        if (state_ != LinkState::Scanning) return;
        for (auto& peer : peers_) {
            if (peer.session_id == p.session_id) {
                peer.host_name = p.host_name;
                peer.rssi_dbm = rssi;
                peer.last_seen_ms = now_ms;
                return;
            }
        }
        peers_.push_back({p.host_name, p.session_id, rssi, now_ms});
        return;
    case PacketType::Join:
        if (p.session_id != session_id_) return;
        if (state_ == LinkState::Beaconing) {
            nonce_ = p.nonce; // first joiner wins
            state_ = LinkState::Connected;
            drop_queued(PacketType::Beacon);
        } else if (state_ != LinkState::Connected || p.nonce != nonce_) {
            return;
        }
        if (!queued(PacketType::Accept)) {
            Packet a;
            a.type = PacketType::Accept;
            a.session_id = session_id_;
            a.nonce = nonce_;
            enqueue(a);
        }
        return;
    case PacketType::Accept:
        if (state_ == LinkState::Joining && p.session_id == session_id_ && p.nonce == nonce_) {
            state_ = LinkState::Connected;
            drop_queued(PacketType::Join);
        }
        return;
    case PacketType::Data:
        if (state_ == LinkState::Connected && p.session_id == session_id_) {
            rx_data_.insert(rx_data_.end(), p.data.begin(), p.data.end());
        }
        return;
    case PacketType::Bye:
        if (state_ == LinkState::Connected && p.session_id == session_id_) {
            state_ = LinkState::Closed;
            tx_queue_.clear();
        }
        return;
    }
}

void LoRaLink::backoff(std::uint64_t now_ms) {
    const std::uint32_t lo = timing_.lbt_backoff_min_ms;
    const std::uint32_t hi = std::max(lo, timing_.lbt_backoff_max_ms);
    next_tx_ms_ = now_ms + lo + rng_() % (hi - lo + 1);
}

void LoRaLink::pump_tx(std::uint64_t now_ms) {
    if (tx_in_flight_ || tx_queue_.empty() || now_ms < next_tx_ms_) return;
    const Entry& e = tx_queue_.front();
    const TxDecision d = policy_.evaluate(now_ms, e.bytes.size());
    if (d.status == TxDecisionStatus::Deferred) {
        next_tx_ms_ = d.not_before_ms;
        return;
    }
    if (d.status != TxDecisionStatus::Allowed) {
        // Policy locked/invalid: nothing may be transmitted any more.
        tx_queue_.clear();
        state_ = LinkState::Failed;
        return;
    }
    switch (radio_.try_start_transmit(e.bytes.data(), e.bytes.size())) {
    case RadioStart::Started:
        policy_.record(now_ms, e.bytes.size());
        tx_in_flight_ = true;
        tx_queue_.pop_front();
        break;
    case RadioStart::Busy:
        backoff(now_ms);
        break;
    case RadioStart::Failed:
        tx_queue_.clear();
        state_ = LinkState::Failed;
        break;
    }
}

void LoRaLink::poll(std::uint64_t now_ms) {
    if (!radio_.ready()) {
        if (state_ != LinkState::Closed) state_ = LinkState::Failed;
        return;
    }

    std::uint8_t buf[kMaxRadioPacket];
    for (;;) {
        const RadioPoll r = radio_.poll(buf, sizeof buf);
        if (r.event == RadioEvent::Idle) break;
        if (r.event == RadioEvent::TxDone) {
            tx_in_flight_ = false;
        } else if (r.event == RadioEvent::Received) {
            if (auto p = decode_packet(buf, r.size)) handle(*p, r.rssi_dbm, now_ms);
        } else if (r.event == RadioEvent::Failed) {
            tx_in_flight_ = false;
        }
    }

    switch (state_) {
    case LinkState::Beaconing:
        if (now_ms >= next_beacon_ms_) {
            next_beacon_ms_ = now_ms + timing_.beacon_interval_ms;
            if (!queued(PacketType::Beacon)) {
                Packet b;
                b.type = PacketType::Beacon;
                b.session_id = session_id_;
                b.host_name = host_name_;
                enqueue(b);
            }
        }
        break;
    case LinkState::Scanning:
        peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                    [&](const LoRaPeer& p) { return now_ms - p.last_seen_ms > timing_.peer_expiry_ms; }),
                     peers_.end());
        break;
    case LinkState::Joining:
        if (now_ms >= next_join_ms_) {
            if (join_attempts_ >= timing_.join_max_attempts) {
                drop_queued(PacketType::Join);
                state_ = LinkState::Failed;
            } else {
                Packet j;
                j.type = PacketType::Join;
                j.session_id = session_id_;
                j.nonce = nonce_;
                if (!queued(PacketType::Join)) enqueue(j);
                ++join_attempts_;
                next_join_ms_ = now_ms + timing_.join_retry_ms;
            }
        }
        break;
    default:
        break;
    }

    pump_tx(now_ms);
}

} // namespace battleship::net::lora
