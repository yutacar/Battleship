// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// PlayerView tests: single-sided state machine correctness, and the critical
// cross-check that driving two PlayerView instances via SHOT/RESULT messages
// produces identical outcomes to a single full Game instance (this is what
// makes networked play safe: neither peer needs the opponent's real Board).

#include "core/player_view.h"

#include <gtest/gtest.h>

#include "core/game.h"

using namespace battleship::core;

namespace {

void place_standard_fleet(Board& board, int row_offset = 0) {
    board.place_ship(Ship(ShipType::Carrier, Coord(0, row_offset + 0), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Battleship, Coord(0, row_offset + 1), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Cruiser, Coord(0, row_offset + 2), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Submarine, Coord(0, row_offset + 3), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, row_offset + 4), Orientation::Horizontal));
}

TEST(PlayerView, StartFailsIfOwnBoardNotFullyPlaced) {
    PlayerView view;
    EXPECT_FALSE(view.start(true));
}

TEST(PlayerView, StartSetsTurnFromGoesFirst) {
    PlayerView view;
    place_standard_fleet(view.own_board());

    EXPECT_TRUE(view.start(true));
    EXPECT_EQ(view.status(), MatchStatus::InProgress);
    EXPECT_TRUE(view.is_my_turn());
}

TEST(PlayerView, StartWithGoesFirstFalseMeansOpponentTurn) {
    PlayerView view;
    place_standard_fleet(view.own_board());
    view.start(false);
    EXPECT_FALSE(view.is_my_turn());
}

TEST(PlayerView, ApplyIncomingShotUpdatesOwnBoardAndFlipsTurnToMine) {
    PlayerView view;
    place_standard_fleet(view.own_board());
    view.start(false);  // opponent goes first

    ShotResult result = view.apply_incoming_shot(Coord(0, 0));  // hits our Carrier
    EXPECT_EQ(result.outcome, ShotOutcome::Hit);
    EXPECT_TRUE(view.is_my_turn());  // opponent's shot resolved, now it's ours
    EXPECT_EQ(view.status(), MatchStatus::InProgress);
}

TEST(PlayerView, ApplyOwnShotResultUpdatesTrackingAndFlipsTurnAway) {
    PlayerView view;
    place_standard_fleet(view.own_board());
    view.start(true);  // our turn

    ShotResult miss;
    miss.outcome = ShotOutcome::Miss;
    view.apply_own_shot_result(Coord(3, 3), miss);

    EXPECT_FALSE(view.is_my_turn());
    EXPECT_EQ(view.tracking_board().mark_at(Coord(3, 3)), CellMark::Miss);
}

TEST(PlayerView, ApplyOwnShotResultWinEndsGame) {
    PlayerView view;
    place_standard_fleet(view.own_board());
    view.start(true);

    ShotResult win;
    win.outcome = ShotOutcome::Sunk;
    win.sunk_ship = ShipType::Destroyer;
    win.game_won = true;
    view.apply_own_shot_result(Coord(9, 9), win);

    EXPECT_EQ(view.status(), MatchStatus::Finished);
    EXPECT_TRUE(view.i_won());
}

TEST(PlayerView, ApplyIncomingShotLossEndsGame) {
    PlayerView view;
    place_standard_fleet(view.own_board());
    view.start(false);

    // Sink our entire fleet via incoming shots; the final shot reports
    // game_won=true from the *shooter's* perspective (i.e. we lost).
    std::vector<Coord> our_cells = {
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1},
        {0, 2}, {1, 2}, {2, 2}, {0, 3}, {1, 3}, {2, 3}, {0, 4}, {1, 4},
    };
    ShotResult last;
    for (const Coord& c : our_cells) {
        last = view.apply_incoming_shot(c);
    }
    EXPECT_TRUE(last.game_won);
    EXPECT_EQ(view.status(), MatchStatus::Finished);
    EXPECT_FALSE(view.i_won());
}

// The critical networked-play safety net: drive two PlayerView objects
// (no shared Board) purely via SHOT/RESULT exchange and compare the final
// outcome to a Game instance driven with the identical shot sequence.
//
// Shot sequence design: each side's shots must, taken together, actually
// cover 100% of the opponent's fleet cells for the match to reach
// MatchStatus::Finished (a real head-to-head match needs enough turns for
// both fleets to be found and sunk). We build a full alternating sequence
// of (One shoots at a Two-fleet cell, Two shoots at a One-fleet cell) pairs
// -- covering every ship cell exactly once each, in shuffled-but-fixed
// order -- interleaved with a handful of deliberate misses, so both Hit,
// Miss, and Sunk outcomes are exercised on both sides before either fleet
// is fully found.
TEST(PlayerView, TwoPlayerViewsMatchFullGameForIdenticalShotSequence) {
    // Two's fleet cells (row_offset=5), the targets One will fire at.
    std::vector<Coord> two_fleet_cells = {
        {0, 5}, {1, 5}, {2, 5}, {3, 5}, {4, 5},  // Carrier
        {0, 6}, {1, 6}, {2, 6}, {3, 6},          // Battleship
        {0, 7}, {1, 7}, {2, 7},                  // Cruiser
        {0, 8}, {1, 8}, {2, 8},                  // Submarine
        {0, 9}, {1, 9},                          // Destroyer
    };
    // One's fleet cells (row_offset=0), the targets Two will fire at.
    std::vector<Coord> one_fleet_cells = {
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0},  // Carrier
        {0, 1}, {1, 1}, {2, 1}, {3, 1},          // Battleship
        {0, 2}, {1, 2}, {2, 2},                  // Cruiser
        {0, 3}, {1, 3}, {2, 3},                  // Submarine
        {0, 4}, {1, 4},                          // Destroyer
    };
    // A few deliberate misses (unoccupied cells) fired by One at Two's board,
    // inserted before the fleet-clearing shots to exercise Miss handling.
    std::vector<Coord> one_misses = {{9, 5}, {9, 6}};
    std::vector<Coord> two_misses = {{9, 0}, {9, 1}};

    // --- Reference: full Game with both real boards. ---
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two), /*row_offset=*/5);
    game.start(PlayerId::One);

    // --- Candidate: two independent PlayerViews. ---
    PlayerView view_one;
    place_standard_fleet(view_one.own_board());
    PlayerView view_two;
    place_standard_fleet(view_two.own_board(), /*row_offset=*/5);
    view_one.start(true);
    view_two.start(false);

    auto fire_one_at_two = [&](Coord target) {
        ShotResult game_result = game.shoot(PlayerId::One, target);
        ShotResult from_view_two = view_two.apply_incoming_shot(target);
        view_one.apply_own_shot_result(target, from_view_two);
        EXPECT_EQ(from_view_two.outcome, game_result.outcome);
        EXPECT_EQ(from_view_two.sunk_ship, game_result.sunk_ship);
        EXPECT_EQ(from_view_two.game_won, game_result.game_won);
        return game_result;
    };
    auto fire_two_at_one = [&](Coord target) {
        ShotResult game_result = game.shoot(PlayerId::Two, target);
        ShotResult from_view_one = view_one.apply_incoming_shot(target);
        view_two.apply_own_shot_result(target, from_view_one);
        EXPECT_EQ(from_view_one.outcome, game_result.outcome);
        EXPECT_EQ(from_view_one.sunk_ship, game_result.sunk_ship);
        EXPECT_EQ(from_view_one.game_won, game_result.game_won);
        return game_result;
    };

    // Deliberate misses first (One/Two strictly alternate turns).
    for (size_t i = 0; i < one_misses.size(); ++i) {
        fire_one_at_two(one_misses[i]);
        fire_two_at_one(two_misses[i]);
    }

    // Now clear both fleets cell-by-cell, alternating turns, until someone
    // wins. One always has equal-or-more remaining targets than Two at each
    // point since both lists are the same length, so this naturally ends
    // when One lands the final Sunk on Two's last ship (One fires first each
    // round in this loop).
    bool finished = false;
    for (size_t i = 0; i < two_fleet_cells.size() && !finished; ++i) {
        ShotResult r1 = fire_one_at_two(two_fleet_cells[i]);
        if (r1.game_won) {
            finished = true;
            break;
        }
        ShotResult r2 = fire_two_at_one(one_fleet_cells[i]);
        if (r2.game_won) {
            finished = true;
        }
    }

    ASSERT_TRUE(finished);
    ASSERT_EQ(game.status(), MatchStatus::Finished);
    if (game.winner() == PlayerId::One) {
        EXPECT_TRUE(view_one.i_won());
        EXPECT_EQ(view_two.status(), MatchStatus::Finished);
        EXPECT_FALSE(view_two.i_won());
    } else {
        EXPECT_TRUE(view_two.i_won());
        EXPECT_EQ(view_one.status(), MatchStatus::Finished);
        EXPECT_FALSE(view_one.i_won());
    }

    // Tracking boards match on shots-fired/hits-recorded counters and on the
    // final confirmed-sunk-ship list -- this is the game-correctness surface
    // networked play depends on (win/loss, turn order, "which ship types are
    // down" for AI/commentary). See PlayerView::apply_own_shot_result for a
    // documented, intentional divergence in *per-cell* CellMark history: Game
    // retroactively upgrades every cell of a just-sunk ship to Sunk (it can,
    // since it holds the opponent's real Board), while PlayerView only ever
    // learns the single cell of the shot that produced the Sunk RESULT and
    // cannot retroactively relabel earlier Hit cells of that same hull
    // without ambiguity when multiple ships are being hunted in parallel --
    // this matches PLAN.md's wire protocol, which carries no cell list
    // beyond the single shot coordinate (RESULT(hit|miss|sunk+shiptype)).
    // A cell-by-cell CellMark comparison would therefore incorrectly fail on
    // those earlier Hit-vs-Sunk cells; we instead assert the counters and
    // sunk-ship set that any UI/AI/commentary consumer actually depends on.
    EXPECT_EQ(view_one.tracking_board().shots_fired(), game.tracking_board(PlayerId::One).shots_fired());
    EXPECT_EQ(view_one.tracking_board().hits_recorded(), game.tracking_board(PlayerId::One).hits_recorded());
    EXPECT_EQ(view_one.tracking_board().sunk_ships(), game.tracking_board(PlayerId::One).sunk_ships());
    EXPECT_EQ(view_two.tracking_board().shots_fired(), game.tracking_board(PlayerId::Two).shots_fired());
    EXPECT_EQ(view_two.tracking_board().hits_recorded(), game.tracking_board(PlayerId::Two).hits_recorded());
    EXPECT_EQ(view_two.tracking_board().sunk_ships(), game.tracking_board(PlayerId::Two).sunk_ships());

    // Every cell that Game marks Sunk, PlayerView must have marked at least
    // Hit (it never "loses" information, it only lags on the final upgrade).
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            Coord c(x, y);
            if (game.tracking_board(PlayerId::One).mark_at(c) == CellMark::Sunk) {
                EXPECT_NE(view_one.tracking_board().mark_at(c), CellMark::Unknown);
                EXPECT_NE(view_one.tracking_board().mark_at(c), CellMark::Miss);
            }
            if (game.tracking_board(PlayerId::Two).mark_at(c) == CellMark::Sunk) {
                EXPECT_NE(view_two.tracking_board().mark_at(c), CellMark::Unknown);
                EXPECT_NE(view_two.tracking_board().mark_at(c), CellMark::Miss);
            }
        }
    }
}

}  // namespace
