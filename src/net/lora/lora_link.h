/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Beacon / scan / join-accept / data / bye state machine on top of IRadio.
 * Headless and non-blocking: the caller supplies a monotonic millisecond
 * clock to every call and drives the link by calling poll() regularly.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include "net/lora/japan920_policy.h"
#include "net/lora/lora_packet.h"
#include "net/lora/radio_iface.h"

namespace battleship::net::lora {

enum class LinkState { Idle, Beaconing, Scanning, Joining, Connected, Closed, Failed };

struct LoRaPeer {
    std::string host_name;
    std::uint16_t session_id = 0;
    std::int16_t rssi_dbm = 0;
    std::uint64_t last_seen_ms = 0;
};

struct LinkTiming {
    std::uint32_t beacon_interval_ms = 2000;
    std::uint32_t peer_expiry_ms = 8000;
    std::uint32_t join_retry_ms = 1000;
    int join_max_attempts = 5;
    std::uint32_t lbt_backoff_min_ms = 10;
    std::uint32_t lbt_backoff_max_ms = 60;
    std::size_t tx_queue_limit = 16;
    std::uint32_t bye_window_ms = 500; // how long a pending BYE is retried after close()
};

class LoRaLink {
public:
    LoRaLink(IRadio& radio, Japan920Policy policy, std::uint32_t seed, LinkTiming timing = {});

    void start_host(const std::string& host_name, std::uint64_t now_ms);
    void start_scan(std::uint64_t now_ms);
    // Starts joining the given session (it need not be in peers()). Returns
    // false unless the link is Idle or Scanning.
    bool join(std::uint16_t session_id, std::uint64_t now_ms);
    void poll(std::uint64_t now_ms);

    LinkState state() const { return state_; }
    std::uint16_t session_id() const { return session_id_; }
    const std::vector<LoRaPeer>& peers() const { return peers_; }

    // Only while Connected; len must be 1..kMaxDataLen. When the transmit
    // queue is full the oldest DATA is dropped (upper layer retransmits).
    bool send_data(const std::uint8_t* data, std::size_t len);
    // Appends all received DATA bytes to out and returns the byte count.
    std::size_t take_received(std::vector<std::uint8_t>& out);
    // State becomes Closed immediately. From Connected, one BYE is attempted
    // during this very call (pump_tx at now_ms). If that attempt cannot start
    // (channel busy, policy deferral, a transmission still in flight) it is
    // retried by poll() for at most timing.bye_window_ms, then dropped
    // silently (the peer's own timeout covers a lost BYE). No other packet is
    // transmitted after close(), and Closed never turns into Failed.
    void close(std::uint64_t now_ms);

private:
    struct Entry {
        std::vector<std::uint8_t> bytes;
        PacketType type;
    };

    void enqueue(const Packet& p);
    bool queued(PacketType t) const;
    void drop_queued(PacketType t);
    void handle(const Packet& p, std::int16_t rssi, std::uint64_t now_ms);
    void pump_tx(std::uint64_t now_ms);
    void backoff(std::uint64_t now_ms);

    IRadio& radio_;
    Japan920Policy policy_;
    std::mt19937 rng_;
    LinkTiming timing_;

    LinkState state_ = LinkState::Idle;
    std::uint16_t session_id_ = 0;
    std::uint16_t nonce_ = 0;
    std::string host_name_;
    std::vector<LoRaPeer> peers_;

    std::deque<Entry> tx_queue_;
    bool tx_in_flight_ = false;
    std::uint64_t next_tx_ms_ = 0;
    std::uint64_t next_beacon_ms_ = 0;
    std::uint64_t next_join_ms_ = 0;
    std::uint64_t bye_deadline_ms_ = 0;
    int join_attempts_ = 0;
    std::vector<std::uint8_t> rx_data_;
};

} // namespace battleship::net::lora
