/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/lora_transport.h"

#include <algorithm>
#include <utility>

namespace battleship::net::lora {

LoRaTransport::LoRaTransport(LoRaLink& link, std::function<std::uint64_t()> clock_ms)
    : link_(link), clock_ms_(std::move(clock_ms)) {}

void LoRaTransport::poll() {
    link_.poll(clock_ms_());
}

TransportState LoRaTransport::state() const {
    switch (link_.state()) {
    case LinkState::Connected:
        return TransportState::Connected;
    case LinkState::Closed:
    case LinkState::Failed:
        return TransportState::Disconnected;
    default:
        return TransportState::Connecting;
    }
}

bool LoRaTransport::send(const std::uint8_t* data, std::size_t len) {
    if (state() != TransportState::Connected || len == 0) return false;
    for (std::size_t off = 0; off < len;) {
        const std::size_t n = std::min<std::size_t>(len - off, kMaxDataLen);
        if (!link_.send_data(data + off, n)) return false;
        off += n;
    }
    return true;
}

std::size_t LoRaTransport::receive(std::vector<std::uint8_t>& out) {
    return link_.take_received(out);
}

void LoRaTransport::close() {
    if (link_.state() == LinkState::Closed) return;
    link_.close(clock_ms_());
}

} // namespace battleship::net::lora
