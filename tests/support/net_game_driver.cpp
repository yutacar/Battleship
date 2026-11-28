/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "support/net_game_driver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include "core/fleet.h"

namespace battleship::testing {

namespace net = battleship::net;
namespace core = battleship::core;

namespace {

/// Deterministic firing strategy: a seeded shuffle of all 100 cells.
class ShuffledTargets {
public:
    explicit ShuffledTargets(uint32_t seed) {
        cells_.resize(core::kBoardSize * core::kBoardSize);
        std::iota(cells_.begin(), cells_.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(cells_.begin(), cells_.end(), rng);
    }

    core::Coord next() {
        const int cell = cells_.at(cursor_++);
        return {cell % core::kBoardSize, cell / core::kBoardSize};
    }

private:
    std::vector<int> cells_;
    size_t cursor_ = 0;
};

}  // namespace

/// Steps both sessions until both leave Handshake+Placement setup and the
/// battle runs to Finished on both sides (or the step budget runs out).
/// Returns the number of poll iterations consumed.
int run_full_game(net::NetSession& host, net::NetSession& client,
                  std::function<void(uint32_t now_ms)> step_fn, uint32_t fire_seed,
                  uint32_t step_ms) {
    constexpr int max_steps = 200000;
    ShuffledTargets host_targets(fire_seed);
    ShuffledTargets client_targets(fire_seed + 7777);
    std::mt19937 place_rng_host(fire_seed + 11);
    std::mt19937 place_rng_client(fire_seed + 22);
    bool host_placed = false;
    bool client_placed = false;

    uint32_t now = 0;
    for (int step = 0; step < max_steps; ++step) {
        step_fn(now);

        if (!host_placed && host.phase() == net::SessionPhase::Placement) {
            core::place_fleet_randomly(host.player_view().own_board(), place_rng_host);
            EXPECT_TRUE(host.send_ready());
            host_placed = true;
        }
        if (!client_placed && client.phase() == net::SessionPhase::Placement) {
            core::place_fleet_randomly(client.player_view().own_board(), place_rng_client);
            EXPECT_TRUE(client.send_ready());
            client_placed = true;
        }

        if (host.can_fire()) {
            core::Coord target = host_targets.next();
            while (host.player_view().tracking_board().was_shot_at(target)) {
                target = host_targets.next();
            }
            EXPECT_TRUE(host.fire(target));
        }
        if (client.can_fire()) {
            core::Coord target = client_targets.next();
            while (client.player_view().tracking_board().was_shot_at(target)) {
                target = client_targets.next();
            }
            EXPECT_TRUE(client.fire(target));
        }

        if (host.phase() == net::SessionPhase::Finished &&
            client.phase() == net::SessionPhase::Finished) {
            return step;
        }
        if (host.phase() == net::SessionPhase::Disconnected ||
            client.phase() == net::SessionPhase::Disconnected) {
            ADD_FAILURE() << "unexpected disconnect at step " << step;
            return step;
        }

        now += step_ms;
    }
    ADD_FAILURE() << "game did not finish within step budget";
    return max_steps;
}

void expect_consistent_final_state(const net::NetSession& host, const net::NetSession& client) {
    auto& host_view = host.player_view();
    auto& client_view = client.player_view();

    // Exactly one winner, and both agree on who it is.
    EXPECT_NE(host_view.i_won(), client_view.i_won());
    EXPECT_EQ(host_view.status(), core::MatchStatus::Finished);
    EXPECT_EQ(client_view.status(), core::MatchStatus::Finished);

    auto& winner = host_view.i_won() ? host_view : client_view;
    auto& loser = host_view.i_won() ? client_view : host_view;

    // The loser's fleet is fully sunk; the winner's is not.
    EXPECT_TRUE(loser.own_board().all_sunk());
    EXPECT_FALSE(winner.own_board().all_sunk());

    // The winner's tracking board confirms all five enemy ships down.
    EXPECT_EQ(winner.tracking_board().sunk_ships().size(),
              static_cast<size_t>(core::kShipTypeCount));
    EXPECT_TRUE(winner.tracking_board().all_ships_sunk());
    EXPECT_LT(loser.tracking_board().sunk_ships().size(),
              static_cast<size_t>(core::kShipTypeCount));

    // Shot bookkeeping matches across the wire: every shot one side sent
    // was received by the other exactly once (dedup working end to end).
    EXPECT_EQ(host.shots_sent(), client.shots_received());
    EXPECT_EQ(client.shots_sent(), host.shots_received());

    // Each side's tracking board mirrors the hits actually scored on the
    // opponent's own board.
    EXPECT_EQ(host_view.tracking_board().shots_fired(), host.shots_sent());
    EXPECT_EQ(client_view.tracking_board().shots_fired(), client.shots_sent());
}

} // namespace battleship::testing
