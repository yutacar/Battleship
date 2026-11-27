/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * NetSession end-to-end tests: two full protocol state machines (HELLO ->
 * READY -> SHOT/RESULT -> GAME_OVER) driven headlessly over loopback
 * transports, including many-seed full games with loss/duplication/
 * fragmentation injection, cross-checking both peers' final PlayerView state
 * for consistency (Phase 4 spec).
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>

#include "core/fleet.h"
#include "net/loopback_transport.h"
#include "net/net_session.h"
#include "support/net_game_driver.h"

namespace net = battleship::net;
namespace core = battleship::core;

namespace {

struct SessionPair {
    std::unique_ptr<net::LoopbackTransport> transport_host;
    std::unique_ptr<net::LoopbackTransport> transport_client;
    std::unique_ptr<net::NetSession> host;
    std::unique_ptr<net::NetSession> client;
};

SessionPair make_session_pair(uint32_t host_seed, uint32_t client_seed,
                              net::LoopbackFaultConfig h_to_c = {},
                              net::LoopbackFaultConfig c_to_h = {},
                              net::SessionConfig config = {}) {
    SessionPair pair;
    auto transports = net::make_loopback_pair(h_to_c, c_to_h);
    pair.transport_host = std::move(transports.first);
    pair.transport_client = std::move(transports.second);
    pair.host = std::make_unique<net::NetSession>(*pair.transport_host, net::SessionRole::Host,
                                                  host_seed, config);
    pair.client = std::make_unique<net::NetSession>(*pair.transport_client, net::SessionRole::Client,
                                                    client_seed, config);
    return pair;
}

/// Loopback-specific wrapper over the shared game driver.
int run_full_game(SessionPair& pair, uint32_t fire_seed, uint32_t step_ms = 25) {
    return battleship::testing::run_full_game(
        *pair.host, *pair.client,
        [&pair](uint32_t now) {
            pair.host->poll(now);
            pair.client->poll(now);
        },
        fire_seed, step_ms);
}

void expect_consistent_final_state(SessionPair& pair) {
    battleship::testing::expect_consistent_final_state(*pair.host, *pair.client);
}

}  // namespace

TEST(NetSession, HandshakeAgreesOnFirstPlayer) {
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }

    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Placement);
    EXPECT_EQ(pair.client->phase(), net::SessionPhase::Placement);
    // Exactly one of the two peers may go first, and they must agree.
    EXPECT_NE(pair.host->goes_first(), pair.client->goes_first());
}

TEST(NetSession, TieBreakerTieFallsBackToHost) {
    // Identical seeds -> identical tie-breaker bytes -> deterministic
    // fallback: the Host side fires first on both peers' view of the world.
    auto pair = make_session_pair(42, 42);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }

    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Placement);
    EXPECT_TRUE(pair.host->goes_first());
    EXPECT_FALSE(pair.client->goes_first());
}

TEST(NetSession, VersionMismatchDisconnects) {
    auto transports = net::make_loopback_pair();
    net::NetSession session(*transports.first, net::SessionRole::Host, 1);

    // Hand-craft a HELLO from a hypothetical future protocol version and
    // inject it raw from the peer side.
    net::Frame hello;
    hello.type = net::MessageType::Hello;
    hello.seq = 0;
    hello.payload = net::encode(net::HelloPayload{net::kProtocolVersion + 1, 99});
    transports.second->send(net::encode_frame(hello));

    session.poll(0);
    session.poll(10);

    EXPECT_EQ(session.phase(), net::SessionPhase::Disconnected);
    EXPECT_EQ(session.disconnect_reason(), net::DisconnectReason::VersionMismatch);
}

TEST(NetSession, ReadySyncWaitsForBothPeers) {
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Placement);

    std::mt19937 rng_host(1);
    std::mt19937 rng_client(2);
    core::place_fleet_randomly(pair.host->player_view().own_board(), rng_host);
    core::place_fleet_randomly(pair.client->player_view().own_board(), rng_client);

    // Host readies first; battle must not start until the client follows.
    EXPECT_TRUE(pair.host->send_ready());
    for (uint32_t now = 100; now <= 300; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::WaitPeerReady);

    EXPECT_TRUE(pair.client->send_ready());
    for (uint32_t now = 300; now <= 500; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Battle);
    EXPECT_EQ(pair.client->phase(), net::SessionPhase::Battle);

    // Turn assignment is complementary.
    EXPECT_NE(pair.host->player_view().is_my_turn(), pair.client->player_view().is_my_turn());
    EXPECT_EQ(pair.host->player_view().is_my_turn(), pair.host->goes_first());
}

TEST(NetSession, SendReadyRequiresFullPlacement) {
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Placement);
    EXPECT_FALSE(pair.host->send_ready());  // no ships placed yet
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Placement);
}

TEST(NetSession, FullGameManySeedsCleanLink) {
    for (uint32_t seed = 0; seed < 20; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        auto pair = make_session_pair(seed * 2 + 1, seed * 2 + 2);
        run_full_game(pair, seed);
        expect_consistent_final_state(pair);
    }
}

TEST(NetSession, FullGameManySeedsLossyDupFragmentedLink) {
    // The LoRa-grade E2E: full games over a link that loses 15%, duplicates
    // 15%, and byte-fragments everything, in both directions.
    net::SessionConfig config;
    config.reliability.ack_timeout_ms = 50;
    config.reliability.max_retransmits = 200;  // soak: patience over failure

    for (uint32_t seed = 0; seed < 8; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        net::LoopbackFaultConfig h_to_c;
        h_to_c.loss_rate = 0.15;
        h_to_c.duplicate_rate = 0.15;
        h_to_c.fragment_bytes = true;
        h_to_c.seed = seed + 1;
        net::LoopbackFaultConfig c_to_h = h_to_c;
        c_to_h.seed = seed + 501;

        auto pair = make_session_pair(seed * 2 + 1, seed * 2 + 2, h_to_c, c_to_h, config);
        run_full_game(pair, seed);
        expect_consistent_final_state(pair);
    }
}

TEST(NetSession, PingKeepsIdleSessionAlive) {
    auto pair = make_session_pair(1, 2);
    // Idle in the Placement phase far longer than disconnect_timeout_ms
    // (10s default): periodic PINGs must keep both sides happy.
    for (uint32_t now = 0; now <= 60000; now += 100) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Placement);
    EXPECT_EQ(pair.client->phase(), net::SessionPhase::Placement);
}

TEST(NetSession, SilentPeerIsDetectedAsDisconnected) {
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Placement);

    // Client process dies (never polls again). The host must notice within
    // the disconnect budget: either its PING retransmits exhaust
    // (SendFailed) or plain receive silence trips the timeout (PeerSilent).
    for (uint32_t now = 100; now <= 30000; now += 100) {
        pair.host->poll(now);
        if (pair.host->phase() == net::SessionPhase::Disconnected) break;
    }
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Disconnected);
    EXPECT_NE(pair.host->disconnect_reason(), net::DisconnectReason::None);
}

TEST(NetSession, TransportDeathIsDetectedImmediately) {
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Placement);

    pair.transport_host->set_state_for_testing(net::TransportState::Disconnected);
    pair.host->poll(200);
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Disconnected);
    EXPECT_EQ(pair.host->disconnect_reason(), net::DisconnectReason::TransportClosed);
}

TEST(NetSession, CannotFireOutOfTurnOrTwiceAtSameCell) {
    auto pair = make_session_pair(1, 2);
    // Bring both to Battle.
    std::mt19937 rng_host(3);
    std::mt19937 rng_client(4);
    bool placed = false;
    for (uint32_t now = 0; now <= 500 && !placed; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
        if (pair.host->phase() == net::SessionPhase::Placement &&
            pair.client->phase() == net::SessionPhase::Placement) {
            core::place_fleet_randomly(pair.host->player_view().own_board(), rng_host);
            core::place_fleet_randomly(pair.client->player_view().own_board(), rng_client);
            EXPECT_TRUE(pair.host->send_ready());
            EXPECT_TRUE(pair.client->send_ready());
            placed = true;
        }
    }
    for (uint32_t now = 500; now <= 1000; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Battle);

    net::NetSession& first = pair.host->goes_first() ? *pair.host : *pair.client;
    net::NetSession& second = pair.host->goes_first() ? *pair.client : *pair.host;

    // Out of turn: the second player cannot fire.
    EXPECT_FALSE(second.can_fire());
    EXPECT_FALSE(second.fire({0, 0}));

    // In turn: first fires; while the RESULT is pending they cannot fire
    // again (stop-and-wait at the game level too).
    EXPECT_TRUE(first.fire({0, 0}));
    EXPECT_FALSE(first.can_fire());
    EXPECT_FALSE(first.fire({1, 0}));

    // Resolve the round trip, then verify the same cell is rejected later.
    for (uint32_t now = 1000; now <= 2000; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    // It's now the second player's turn; play their shot, come back around.
    ASSERT_TRUE(second.can_fire());
    EXPECT_TRUE(second.fire({9, 9}));
    for (uint32_t now = 2000; now <= 3000; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_TRUE(first.can_fire());
    EXPECT_FALSE(first.fire({0, 0}));  // already targeted that cell
    EXPECT_TRUE(first.fire({1, 0}));
}

// ---- Esc-key convention (Phase 7, App Submission Guide): abandon() ----
// "通信対戦の中断は相手側に正しく伝わる（abandon → 相手は切断/終了を検知）"
// -- these exercise NetSession::abandon() the same way
// BaseViewModel::confirm_interrupt()/request_safe_quit() call it: a local
// player backs out mid-match via the interrupt-confirm modal or long-Esc/
// Home, and the peer -- who never chose to leave -- must still converge to
// Finished quickly rather than sit waiting out the 10s PeerSilent timeout.

TEST(NetSession, AbandonDuringBattleNotifiesPeerImmediately) {
    auto pair = make_session_pair(1, 2);
    std::mt19937 rng_host(3);
    std::mt19937 rng_client(4);
    bool placed = false;
    for (uint32_t now = 0; now <= 500 && !placed; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
        if (pair.host->phase() == net::SessionPhase::Placement &&
            pair.client->phase() == net::SessionPhase::Placement) {
            core::place_fleet_randomly(pair.host->player_view().own_board(), rng_host);
            core::place_fleet_randomly(pair.client->player_view().own_board(), rng_client);
            EXPECT_TRUE(pair.host->send_ready());
            EXPECT_TRUE(pair.client->send_ready());
            placed = true;
        }
    }
    for (uint32_t now = 500; now <= 1000; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Battle);
    ASSERT_EQ(pair.client->phase(), net::SessionPhase::Battle);

    // Host's local player abandons the match (Esc -> "Yes" on the
    // interrupt-confirm modal). The local transition to Finished must be
    // immediate -- confirm_interrupt() shows the title screen right away,
    // it does not wait on a network round trip.
    pair.host->abandon(1000);
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Finished);

    // The client never abandoned anything and has not seen the GameOver
    // frame yet -- still mid-battle from its own point of view.
    EXPECT_EQ(pair.client->phase(), net::SessionPhase::Battle);

    // One poll pair delivers the best-effort GameOver frame abandon()
    // already pushed onto the wire: the peer must converge to Finished
    // quickly (well inside the 10s PeerSilent timeout), not hang waiting
    // for a partner that already left.
    for (uint32_t now = 1010; now <= 1200; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
        if (pair.client->phase() == net::SessionPhase::Finished) break;
    }
    EXPECT_EQ(pair.client->phase(), net::SessionPhase::Finished);
}

TEST(NetSession, AbandonDuringPlacementAlsoNotifiesPeer) {
    // Esc during Placement/Handover on a network match is also
    // interruptible (BaseViewModel::is_match_interruptible(): "any live
    // network flow on a match page needs a confirmation"), so abandon() must
    // work before the battle even starts, not just mid-Battle.
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Placement);
    ASSERT_EQ(pair.client->phase(), net::SessionPhase::Placement);

    pair.host->abandon(100);
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Finished);

    for (uint32_t now = 110; now <= 300; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
        if (pair.client->phase() == net::SessionPhase::Finished) break;
    }
    EXPECT_EQ(pair.client->phase(), net::SessionPhase::Finished);
}

TEST(NetSession, AbandonIsNoOpWhenAlreadyFinishedOrDisconnected) {
    auto pair = make_session_pair(1, 2);
    for (uint32_t now = 0; now <= 100; now += 10) {
        pair.host->poll(now);
        pair.client->poll(now);
    }
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Placement);

    pair.transport_host->set_state_for_testing(net::TransportState::Disconnected);
    pair.host->poll(200);
    ASSERT_EQ(pair.host->phase(), net::SessionPhase::Disconnected);

    // request_safe_quit() (long Esc / Home) calls abandon() unconditionally
    // from anywhere, including a session that already tore itself down --
    // this must stay a no-op rather than resurrect the session or crash.
    pair.host->abandon(210);
    EXPECT_EQ(pair.host->phase(), net::SessionPhase::Disconnected);
}
