/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * ITransport adapter over a LoRaLink: lets NetSession (framing, reliability,
 * game protocol) run unchanged over LoRa. The link must already have been
 * started/joined by the owner; this class only maps link state to transport
 * state and moves bytes.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "net/lora/lora_link.h"
#include "net/net_session.h"
#include "net/transport.h"

namespace battleship::net::lora {

// Session timings tuned for LoRa SF7 airtime and half-duplex turnaround.
inline SessionConfig lora_session_config() {
    SessionConfig c;
    c.ping_interval_ms = 5000;
    c.disconnect_timeout_ms = 20000;
    c.reliability.ack_timeout_ms = 800;
    c.reliability.max_retransmits = 6;
    return c;
}

class LoRaTransport final : public ITransport {
public:
    // `link` must outlive the transport. `clock_ms` is a monotonic millisecond
    // clock, used to drive the link and to stamp close().
    LoRaTransport(LoRaLink& link, std::function<std::uint64_t()> clock_ms);

    // Polls the link with the current clock. The owner calls this (directly or
    // via NetSession::poll) regularly.
    void poll() override;

    // Connected -> Connected; Closed/Failed -> Disconnected; otherwise Connecting.
    TransportState state() const override;

    // Splits `data` into DATA packets of at most kMaxDataLen bytes (normally a
    // game frame fits in one). Returns false if the link is not Connected or a
    // chunk was rejected.
    bool send(const std::uint8_t* data, std::size_t len) override;
    using ITransport::send;

    std::size_t receive(std::vector<std::uint8_t>& out) override;

    // Closes the link (a BYE is attempted immediately; see LoRaLink::close).
    // If the BYE cannot start right away it is retried only while someone keeps
    // polling the link for up to LinkTiming::bye_window_ms, so the owner must
    // keep calling poll() for that long if a prompt BYE matters. Idempotent.
    void close() override;

private:
    LoRaLink& link_;
    std::function<std::uint64_t()> clock_ms_;
};

} // namespace battleship::net::lora
