/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/udp_sim_radio.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace battleship::net::lora {

namespace {
constexpr char kGroup[] = "239.255.53.60";
constexpr std::size_t kIdLen = 4;
constexpr std::int16_t kSimRssi = -60;
} // namespace

UdpSimRadio::UdpSimRadio(double loss_rate, std::uint16_t port)
    : port_(port), loss_rate_(loss_rate) {
    std::random_device rd;
    instance_id_ = rd();
    rng_.seed(rd());

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(kGroup);
    mreq.imr_interface.s_addr = htonl(INADDR_LOOPBACK);
    unsigned char loop = 1;
    unsigned char ttl = 0; // never leaves this host
    in_addr loif{};
    loif.s_addr = htonl(INADDR_LOOPBACK);
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0 ||
        ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) != 0 ||
        ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0 ||
        ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &loif, sizeof(loif)) != 0 || flags < 0 ||
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        ::close(fd);
        return;
    }
    fd_ = fd;
}

UdpSimRadio::~UdpSimRadio() {
    shutdown();
}

void UdpSimRadio::shutdown() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    tx_active_ = false;
}

RadioStart UdpSimRadio::try_start_transmit(const std::uint8_t* data, std::size_t size) {
    if (fd_ < 0 || data == nullptr || size == 0 || size > kMaxRadioPacket) return RadioStart::Failed;
    if (tx_active_) return RadioStart::Busy;

    std::uint8_t buf[kIdLen + kMaxRadioPacket];
    std::memcpy(buf, &instance_id_, kIdLen);
    std::memcpy(buf + kIdLen, data, size);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(kGroup);
    dst.sin_port = htons(port_);
    ssize_t n = ::sendto(fd_, buf, kIdLen + size, 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    if (n != static_cast<ssize_t>(kIdLen + size)) return RadioStart::Failed;

    tx_active_ = true;
    tx_done_at_ = Clock::now() + std::chrono::milliseconds(policy_.airtime_ms(size));
    return RadioStart::Started;
}

RadioPoll UdpSimRadio::poll(std::uint8_t* dst, std::size_t cap) {
    RadioPoll out;
    if (fd_ < 0) return out;
    if (tx_active_ && Clock::now() >= tx_done_at_) {
        tx_active_ = false;
        out.event = RadioEvent::TxDone;
        return out;
    }
    std::uint8_t buf[kIdLen + 256];
    for (;;) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) return out; // EAGAIN or error: nothing to deliver
        if (static_cast<std::size_t>(n) <= kIdLen) continue;
        std::uint32_t id;
        std::memcpy(&id, buf, kIdLen);
        if (id == instance_id_) continue;
        std::size_t len = static_cast<std::size_t>(n) - kIdLen;
        if (len > kMaxRadioPacket || len > cap) continue;
        if (loss_rate_ > 0.0 && std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < loss_rate_)
            continue;
        std::memcpy(dst, buf + kIdLen, len);
        out.event = RadioEvent::Received;
        out.size = len;
        out.rssi_dbm = kSimRssi;
        return out;
    }
}

} // namespace battleship::net::lora
