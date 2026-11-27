/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * ITransport: the byte-stream abstraction every higher net/ layer (framing,
 * reliability, NetSession) is built on (PLAN.md §2.2 "ITransport 抽象").
 * Implementations only need to move raw bytes; framing/CRC/reliability are
 * handled above this layer so the same protocol works unmodified over any
 * transport, including a future lossy/low-bandwidth/half-duplex LoRa serial
 * link (PLAN.md §2.2, §7).
 *
 * Deliberately non-blocking/poll-shaped: `send` enqueues (or attempts an
 * immediate write) and returns immediately; `receive` drains whatever has
 * arrived so far without blocking. This matches driving the transport from
 * an LVGL timer tick (see NetSession) rather than a dedicated blocking I/O
 * thread, and maps naturally onto a half-duplex serial LoRa module later.
 */
#pragma once

#include <cstdint>
#include <vector>

namespace battleship::net {

/// Current connection state of a transport.
enum class TransportState {
    Connecting,   ///< handshake/connect in progress (e.g. BLE GATT connect pending)
    Connected,    ///< ready to send/receive
    Disconnected, ///< closed or failed; no further send/receive will succeed
};

/// Byte-stream transport abstraction. All methods are safe to call from a
/// single-threaded poll loop; implementations must not block.
class ITransport {
public:
    virtual ~ITransport() = default;

    /// Must be called periodically (e.g. from an LVGL timer) to service
    /// non-blocking I/O (accept/connect progress, socket reads/writes).
    virtual void poll() = 0;

    [[nodiscard]] virtual TransportState state() const = 0;

    /// Queues/writes `len` bytes. Returns true if the bytes were accepted
    /// (queued or written) -- not a delivery guarantee; the reliability
    /// layer above this is responsible for that (PLAN.md §2.2).
    virtual bool send(const uint8_t* data, size_t len) = 0;
    bool send(const std::vector<uint8_t>& data) { return send(data.data(), data.size()); }

    /// Appends any bytes received since the last call to `out` and clears
    /// the internal receive buffer. Returns the number of bytes appended.
    virtual size_t receive(std::vector<uint8_t>& out) = 0;

    /// Closes the transport. Idempotent.
    virtual void close() = 0;
};

}  // namespace battleship::net
