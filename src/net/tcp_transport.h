/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * TcpTransport: POSIX non-blocking TCP implementation of ITransport
 * (PLAN.md §2.2 "TcpTransport -- v1 本命"). Two modes:
 *   - listen(port): host side; binds + listens, then accepts exactly one
 *     peer connection (state Connecting until accept succeeds).
 *   - connect(host, port): client side; non-blocking connect (state
 *     Connecting until the handshake completes or fails).
 *
 * All socket I/O is serviced from poll() (never blocks): reads drain into an
 * internal receive buffer, writes flush an internal send buffer, so the
 * caller can drive this from an LVGL timer tick without ever stalling the UI
 * thread (Phase 4 requirement). Works on macOS (desktop 2-process testing)
 * and Linux (CM0 device) alike -- plain BSD sockets, no platform extras.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/transport.h"

namespace battleship::net {

class TcpTransport : public ITransport {
public:
    TcpTransport() = default;
    ~TcpTransport() override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    /// Host side: bind + listen on `port` (0 = OS-assigned; see
    /// local_port()). Returns false on immediate failure (port in use,
    /// etc.). On success, state() == Connecting until a peer is accepted.
    bool listen(uint16_t port);

    /// Client side: begin a non-blocking connect to host:port. `host` is an
    /// IPv4 dotted-quad or hostname. Returns false on immediate failure
    /// (unresolvable name, etc.). On success, state() == Connecting until
    /// the TCP handshake completes (-> Connected) or fails (-> Disconnected).
    bool connect(const std::string& host, uint16_t port);

    /// The locally bound listen port (valid after a successful listen();
    /// useful when listen(0) asked the OS to pick).
    [[nodiscard]] uint16_t local_port() const { return local_port_; }

    /// Peer address as "ip:port" once Connected (diagnostics/UI).
    [[nodiscard]] const std::string& peer_address() const { return peer_address_; }

    void poll() override;
    [[nodiscard]] TransportState state() const override { return state_; }
    using ITransport::send;  // keep the std::vector convenience overload visible
    bool send(const uint8_t* data, size_t len) override;
    size_t receive(std::vector<uint8_t>& out) override;
    void close() override;

private:
    void service_accept();
    void service_connect();
    void service_io();
    void fail();

    int listen_fd_ = -1;
    int fd_ = -1;
    bool connect_in_progress_ = false;
    uint16_t local_port_ = 0;
    std::string peer_address_;

    std::vector<uint8_t> send_buffer_;
    std::vector<uint8_t> recv_buffer_;

    TransportState state_ = TransportState::Disconnected;
};

}  // namespace battleship::net
