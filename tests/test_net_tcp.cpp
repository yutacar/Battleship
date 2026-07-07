/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * TcpTransport smoke tests over real localhost sockets (in-process, one
 * listener + one connector). Deliberately small: the protocol logic itself
 * is exercised exhaustively over LoopbackTransport (test_net_session.cpp);
 * these only prove the POSIX socket plumbing (non-blocking accept/connect,
 * buffered send/recv, orderly-shutdown detection) actually works, ahead of
 * the 2-process desktop verification.
 */
#include <gtest/gtest.h>

#include "core/fleet.h"
#include "net/net_session.h"
#include "net/tcp_transport.h"

#include <random>

namespace net = battleship::net;
namespace core = battleship::core;

namespace {

/// Polls both transports until `predicate` holds or ~2s of real time passes.
template <typename Predicate>
bool pump_until(net::TcpTransport& a, net::TcpTransport& b, Predicate predicate) {
    for (int i = 0; i < 2000; ++i) {
        a.poll();
        b.poll();
        if (predicate()) return true;
    }
    return false;
}

}  // namespace

TEST(NetTcp, ListenConnectExchangeBytes) {
    net::TcpTransport host;
    ASSERT_TRUE(host.listen(0));  // OS-assigned port
    ASSERT_GT(host.local_port(), 0);
    EXPECT_EQ(host.state(), net::TransportState::Connecting);

    net::TcpTransport client;
    ASSERT_TRUE(client.connect("127.0.0.1", host.local_port()));

    ASSERT_TRUE(pump_until(host, client, [&] {
        return host.state() == net::TransportState::Connected &&
               client.state() == net::TransportState::Connected;
    }));

    const std::vector<uint8_t> ping = {1, 2, 3, 4, 5};
    ASSERT_TRUE(client.send(ping));

    std::vector<uint8_t> got;
    ASSERT_TRUE(pump_until(host, client, [&] {
        host.receive(got);
        return got.size() >= ping.size();
    }));
    EXPECT_EQ(got, ping);

    // And the reverse direction.
    const std::vector<uint8_t> pong = {9, 8, 7};
    ASSERT_TRUE(host.send(pong));
    std::vector<uint8_t> got_back;
    ASSERT_TRUE(pump_until(host, client, [&] {
        client.receive(got_back);
        return got_back.size() >= pong.size();
    }));
    EXPECT_EQ(got_back, pong);
}

TEST(NetTcp, PeerCloseIsDetected) {
    net::TcpTransport host;
    ASSERT_TRUE(host.listen(0));
    net::TcpTransport client;
    ASSERT_TRUE(client.connect("127.0.0.1", host.local_port()));
    ASSERT_TRUE(pump_until(host, client, [&] {
        return host.state() == net::TransportState::Connected &&
               client.state() == net::TransportState::Connected;
    }));

    client.close();
    ASSERT_TRUE(pump_until(host, client, [&] {
        return host.state() == net::TransportState::Disconnected;
    }));
}

TEST(NetTcp, ConnectToClosedPortFails) {
    // Grab a free port, then close it again so nobody is listening there.
    uint16_t dead_port = 0;
    {
        net::TcpTransport probe;
        ASSERT_TRUE(probe.listen(0));
        dead_port = probe.local_port();
    }

    net::TcpTransport client;
    // connect() may fail immediately (rare) or report Disconnected shortly
    // after the non-blocking connect resolves with ECONNREFUSED.
    if (client.connect("127.0.0.1", dead_port)) {
        for (int i = 0; i < 2000 && client.state() == net::TransportState::Connecting; ++i) {
            client.poll();
        }
    }
    EXPECT_EQ(client.state(), net::TransportState::Disconnected);
}

TEST(NetTcp, FullNetSessionHandshakeOverLocalhost) {
    net::TcpTransport host_transport;
    ASSERT_TRUE(host_transport.listen(0));
    net::TcpTransport client_transport;
    ASSERT_TRUE(client_transport.connect("127.0.0.1", host_transport.local_port()));

    net::NetSession host(host_transport, net::SessionRole::Host, 11);
    net::NetSession client(client_transport, net::SessionRole::Client, 22);

    uint32_t now = 0;
    for (int i = 0; i < 5000; ++i) {
        host.poll(now);
        client.poll(now);
        now += 1;
        if (host.phase() == net::SessionPhase::Placement &&
            client.phase() == net::SessionPhase::Placement) {
            break;
        }
    }
    ASSERT_EQ(host.phase(), net::SessionPhase::Placement);
    ASSERT_EQ(client.phase(), net::SessionPhase::Placement);
    EXPECT_NE(host.goes_first(), client.goes_first());
}
