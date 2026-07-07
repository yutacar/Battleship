// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// MatchSnapshot / PlayerSnapshot tests: verifies turn counters, remaining
// ship lists, sunk-ship lists, and the flattened own-board/tracking-board
// contents used by the result screen and (later) LLM commentary input.

#include "core/snapshot.h"

#include <gtest/gtest.h>

#include "core/game.h"
#include "core/player_view.h"

using namespace battleship::core;

namespace {

void place_standard_fleet(Board& board, int row_offset = 0) {
    board.place_ship(Ship(ShipType::Carrier, Coord(0, row_offset + 0), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Battleship, Coord(0, row_offset + 1), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Cruiser, Coord(0, row_offset + 2), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Submarine, Coord(0, row_offset + 3), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, row_offset + 4), Orientation::Horizontal));
}

TEST(Snapshot, InitialSnapshotReflectsPlacementPhase) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));

    MatchSnapshot snap = game.snapshot();
    EXPECT_EQ(snap.status, MatchStatus::Placement);
    EXPECT_EQ(snap.turn_number, 0);
    EXPECT_EQ(snap.player_one.own_ships_remaining.size(), static_cast<size_t>(kShipTypeCount));
    EXPECT_TRUE(snap.player_one.enemy_ships_sunk.empty());
    EXPECT_EQ(snap.player_one.shots_fired, 0);
}

TEST(Snapshot, OwnBoardViewMarksShipCellsAndHitFlag) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));  // Carrier at row 0: (0,0)-(4,0)
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::Two);  // Two goes first so it can fire at One's board below

    // Two fires at One's carrier cell (0,0).
    game.shoot(PlayerId::Two, Coord(0, 0));

    MatchSnapshot snap = game.snapshot();
    const OwnBoardView& own = snap.player_one.own_board;

    int idx_hit_ship_cell = 0 * kBoardSize + 0;   // (0,0): ship + hit
    int idx_ship_cell = 0 * kBoardSize + 1;       // (1,0): ship, not hit
    int idx_empty_cell = 9 * kBoardSize + 9;      // (9,9): no ship

    EXPECT_TRUE(own[idx_hit_ship_cell].has_ship);
    EXPECT_TRUE(own[idx_hit_ship_cell].was_hit);

    EXPECT_TRUE(own[idx_ship_cell].has_ship);
    EXPECT_FALSE(own[idx_ship_cell].was_hit);

    EXPECT_FALSE(own[idx_empty_cell].has_ship);
    EXPECT_FALSE(own[idx_empty_cell].was_hit);
}

TEST(Snapshot, TrackingBoardViewReflectsShotsFired) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    game.shoot(PlayerId::One, Coord(0, 0));  // hit on Two's carrier
    game.shoot(PlayerId::Two, Coord(5, 5));  // miss on One (row 5 empty)

    MatchSnapshot snap = game.snapshot();
    int idx_hit = 0 * kBoardSize + 0;
    int idx_unknown = 3 * kBoardSize + 3;

    EXPECT_EQ(snap.player_one.tracking_board[idx_hit], CellMark::Hit);
    EXPECT_EQ(snap.player_one.tracking_board[idx_unknown], CellMark::Unknown);
    EXPECT_EQ(snap.player_one.shots_fired, 1);
    EXPECT_EQ(snap.player_one.hits_recorded, 1);
}

TEST(Snapshot, TurnNumberIncrementsPerShotAcrossBothPlayers) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    game.shoot(PlayerId::One, Coord(9, 9));  // miss
    game.shoot(PlayerId::Two, Coord(9, 9));  // miss

    MatchSnapshot snap = game.snapshot();
    EXPECT_EQ(snap.turn_number, 2);
    EXPECT_EQ(snap.current_turn, PlayerId::One);
}

TEST(Snapshot, RemainingShipsShrinksAsShipsAreSunk) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    game.shoot(PlayerId::One, Coord(0, 4));  // Two's destroyer, cell 1/2
    game.shoot(PlayerId::Two, Coord(9, 9));
    game.shoot(PlayerId::One, Coord(1, 4));  // Two's destroyer, cell 2/2 -> sunk

    MatchSnapshot snap = game.snapshot();
    // Player Two's own_ships_remaining should no longer include Destroyer.
    const auto& two_remaining = snap.player_two.own_ships_remaining;
    EXPECT_EQ(two_remaining.size(), static_cast<size_t>(kShipTypeCount - 1));
    for (ShipType type : two_remaining) {
        EXPECT_NE(type, ShipType::Destroyer);
    }
    // Player One's enemy_ships_sunk (tracking board) should include Destroyer.
    ASSERT_EQ(snap.player_one.enemy_ships_sunk.size(), 1u);
    EXPECT_EQ(snap.player_one.enemy_ships_sunk[0], ShipType::Destroyer);
}

TEST(Snapshot, PlayerViewSnapshotMatchesGamePlayerSnapshotContent) {
    PlayerView view;
    place_standard_fleet(view.own_board());
    view.start(true);

    ShotResult miss;
    miss.outcome = ShotOutcome::Miss;
    view.apply_own_shot_result(Coord(7, 7), miss);

    PlayerSnapshot snap = view.snapshot();
    int idx_ship = 0 * kBoardSize + 0;
    EXPECT_TRUE(snap.own_board[idx_ship].has_ship);
    EXPECT_EQ(snap.tracking_board[7 * kBoardSize + 7], CellMark::Miss);
    EXPECT_EQ(snap.shots_fired, 1);
    EXPECT_EQ(snap.own_ships_remaining.size(), static_cast<size_t>(kShipTypeCount));
}

}  // namespace
