/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/tcp_transport.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace battleship::net {

namespace {

bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void enable_nodelay(int fd) {
    // Small frames (a handful of bytes) exchanged in a strict request/reply
    // pattern are exactly the case Nagle's algorithm hurts: without this,
    // each SHOT/RESULT/ACK can sit in the kernel for up to ~40ms waiting to
    // coalesce. Purely a latency nicety; correctness never depends on it.
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

std::string address_to_string(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

}  // namespace

TcpTransport::~TcpTransport() { close(); }

bool TcpTransport::listen(uint16_t port) {
    close();

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd_, 1) != 0 || !set_nonblocking(listen_fd_)) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
        local_port_ = ntohs(bound.sin_port);
    } else {
        local_port_ = port;
    }

    state_ = TransportState::Connecting;
    return true;
}

bool TcpTransport::connect(const std::string& host, uint16_t port) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    // Note: getaddrinfo can block on DNS for non-numeric names. In practice
    // the UI passes either a discovered peer's numeric IP or a user-typed
    // numeric IP (Direct IP screen), which resolve instantly; hostnames are
    // accepted as a convenience for developers on the command line.
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results) != 0 || !results) {
        return false;
    }

    fd_ = ::socket(results->ai_family, results->ai_socktype, results->ai_protocol);
    if (fd_ < 0 || !set_nonblocking(fd_)) {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        freeaddrinfo(results);
        return false;
    }

    const int rc = ::connect(fd_, results->ai_addr, results->ai_addrlen);
    if (rc == 0) {
        enable_nodelay(fd_);
        if (results->ai_addr->sa_family == AF_INET) {
            peer_address_ = address_to_string(*reinterpret_cast<sockaddr_in*>(results->ai_addr));
        }
        state_ = TransportState::Connected;
        connect_in_progress_ = false;
    } else if (errno == EINPROGRESS) {
        if (results->ai_addr->sa_family == AF_INET) {
            peer_address_ = address_to_string(*reinterpret_cast<sockaddr_in*>(results->ai_addr));
        }
        state_ = TransportState::Connecting;
        connect_in_progress_ = true;
    } else {
        ::close(fd_);
        fd_ = -1;
        freeaddrinfo(results);
        return false;
    }
    freeaddrinfo(results);
    return true;
}

void TcpTransport::service_accept() {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    const int accepted = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (accepted < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;  // nobody yet
        fail();
        return;
    }

    if (!set_nonblocking(accepted)) {
        ::close(accepted);
        fail();
        return;
    }
    enable_nodelay(accepted);

    fd_ = accepted;
    peer_address_ = address_to_string(peer);
    ::close(listen_fd_);  // exactly one opponent per match
    listen_fd_ = -1;
    state_ = TransportState::Connected;
}

void TcpTransport::service_connect() {
    // A non-blocking connect() resolves via writability + SO_ERROR.
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(fd_, &write_set);
    timeval zero{0, 0};
    const int ready = ::select(fd_ + 1, nullptr, &write_set, nullptr, &zero);
    if (ready <= 0) return;  // still in progress (or transient select error)

    int err = 0;
    socklen_t err_len = sizeof(err);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0) {
        fail();
        return;
    }
    enable_nodelay(fd_);
    connect_in_progress_ = false;
    state_ = TransportState::Connected;
}

void TcpTransport::service_io() {
    // Flush pending writes.
    while (!send_buffer_.empty()) {
        const ssize_t written = ::send(fd_, send_buffer_.data(), send_buffer_.size(), 0);
        if (written > 0) {
            send_buffer_.erase(send_buffer_.begin(), send_buffer_.begin() + written);
        } else if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            break;  // kernel buffer full; retry next poll
        } else {
            fail();
            return;
        }
    }

    // Drain available reads.
    uint8_t chunk[2048];
    while (true) {
        const ssize_t got = ::recv(fd_, chunk, sizeof(chunk), 0);
        if (got > 0) {
            recv_buffer_.insert(recv_buffer_.end(), chunk, chunk + got);
        } else if (got == 0) {
            fail();  // orderly shutdown by peer == disconnect for our purposes
            return;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            break;  // nothing more right now
        } else {
            fail();
            return;
        }
    }
}

void TcpTransport::poll() {
    switch (state_) {
        case TransportState::Connecting:
            if (listen_fd_ >= 0) {
                service_accept();
            } else if (fd_ >= 0 && connect_in_progress_) {
                service_connect();
            }
            break;
        case TransportState::Connected:
            service_io();
            break;
        case TransportState::Disconnected:
            break;
    }
}

bool TcpTransport::send(const uint8_t* data, size_t len) {
    if (state_ != TransportState::Connected) return false;
    send_buffer_.insert(send_buffer_.end(), data, data + len);
    service_io();  // opportunistic immediate flush
    return state_ == TransportState::Connected;
}

size_t TcpTransport::receive(std::vector<uint8_t>& out) {
    const size_t n = recv_buffer_.size();
    out.insert(out.end(), recv_buffer_.begin(), recv_buffer_.end());
    recv_buffer_.clear();
    return n;
}

void TcpTransport::close() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connect_in_progress_ = false;
    send_buffer_.clear();
    // Keep recv_buffer_: bytes already received stay drainable after close.
    state_ = TransportState::Disconnected;
}

void TcpTransport::fail() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connect_in_progress_ = false;
    state_ = TransportState::Disconnected;
}

}  // namespace battleship::net
