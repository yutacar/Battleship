// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Game (two-sided, single-process match state machine) tests: placement
// gating, turn order enforcement, shot outcomes, and full-fleet win
// detection driving MatchStatus::Finished.

#include "core/game.h"

#include <gtest/gtest.h>

using namespace battleship::core;

namespace {

void place_standard_fleet(Board& board, int row_offset = 0) {
    board.place_ship(Ship(ShipType::Carrier, Coord(0, row_offset + 0), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Battleship, Coord(0, row_offset + 1), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Cruiser, Coord(0, row_offset + 2), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Submarine, Coord(0, row_offset + 3), Orientation::Horizontal));
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, row_offset + 4), Orientation::Horizontal));
}

TEST(Game, StartFailsIfBoardsNotFullyPlaced) {
    Game game;
    EXPECT_FALSE(game.start(PlayerId::One));
    EXPECT_EQ(game.status(), MatchStatus::Placement);
}

TEST(Game, StartSucceedsOnceBothFullyPlaced) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));

    EXPECT_TRUE(game.start(PlayerId::One));
    EXPECT_EQ(game.status(), MatchStatus::InProgress);
    EXPECT_EQ(game.current_turn(), PlayerId::One);
    EXPECT_EQ(game.turn_number(), 0);
}

TEST(Game, StartTwiceFails) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    ASSERT_TRUE(game.start(PlayerId::One));
    EXPECT_FALSE(game.start(PlayerId::Two));
    EXPECT_EQ(game.current_turn(), PlayerId::One);  // unchanged
}

TEST(Game, ShootOutOfTurnIsNoOpAndDoesNotAdvanceState) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    ShotResult result = game.shoot(PlayerId::Two, Coord(0, 0));
    EXPECT_FALSE(result.game_won);
    EXPECT_EQ(game.current_turn(), PlayerId::One);  // unchanged: still One's turn
    EXPECT_EQ(game.turn_number(), 0);               // no shot was actually applied
    // The opponent board (belongs to Two, target of a would-be shot from One)
    // was not touched by this rejected call.
    EXPECT_FALSE(game.board(PlayerId::One).was_shot_at(Coord(0, 0)));
}

TEST(Game, ShootBeforeStartIsNoOp) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    // status() still Placement, never called start().
    ShotResult result = game.shoot(PlayerId::One, Coord(0, 0));
    EXPECT_FALSE(result.game_won);
    EXPECT_EQ(game.status(), MatchStatus::Placement);
}

TEST(Game, ShootAdvancesTurnAndTrackingBoard) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    // Player Two's carrier occupies row 0 -- One fires at (0,0), a hit.
    ShotResult result = game.shoot(PlayerId::One, Coord(0, 0));
    EXPECT_EQ(result.outcome, ShotOutcome::Hit);
    EXPECT_EQ(game.current_turn(), PlayerId::Two);
    EXPECT_EQ(game.turn_number(), 1);
    EXPECT_EQ(game.tracking_board(PlayerId::One).mark_at(Coord(0, 0)), CellMark::Hit);
}

TEST(Game, ShootMissAlsoAdvancesTurn) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    // Row 9 is empty in the standard layout (ships occupy rows 0-4).
    ShotResult result = game.shoot(PlayerId::One, Coord(5, 9));
    EXPECT_EQ(result.outcome, ShotOutcome::Miss);
    EXPECT_EQ(game.current_turn(), PlayerId::Two);
    EXPECT_EQ(game.turn_number(), 1);
}

TEST(Game, AlreadyShotDoesNotAdvanceTurn) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    game.shoot(PlayerId::One, Coord(5, 9));  // miss, turn -> Two
    game.shoot(PlayerId::Two, Coord(5, 9));  // miss (Two's own shot at One's row 9), turn -> One
    ShotResult repeat = game.shoot(PlayerId::One, Coord(5, 9));
    EXPECT_EQ(repeat.outcome, ShotOutcome::AlreadyShot);
    EXPECT_EQ(game.current_turn(), PlayerId::One);  // unchanged, still One's turn
    EXPECT_EQ(game.turn_number(), 2);               // unchanged from before repeat attempt
}

TEST(Game, SunkReportsCorrectShipTypeAndMarksTrackingBoard) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    // Sink Two's destroyer (row 4, cells (0,4) and (1,4)).
    game.shoot(PlayerId::One, Coord(0, 4));
    game.shoot(PlayerId::Two, Coord(0, 0));  // Two's turn, arbitrary shot back at One
    ShotResult sunk = game.shoot(PlayerId::One, Coord(1, 4));

    EXPECT_EQ(sunk.outcome, ShotOutcome::Sunk);
    EXPECT_EQ(sunk.sunk_ship, ShipType::Destroyer);
    EXPECT_FALSE(sunk.game_won);

    const TrackingBoard& tracking = game.tracking_board(PlayerId::One);
    EXPECT_EQ(tracking.mark_at(Coord(0, 4)), CellMark::Sunk);
    EXPECT_EQ(tracking.mark_at(Coord(1, 4)), CellMark::Sunk);
    ASSERT_EQ(tracking.sunk_ships().size(), 1u);
    EXPECT_EQ(tracking.sunk_ships()[0], ShipType::Destroyer);
}

// Sink all 5 of player Two's ships and verify the game correctly declares
// player One the winner exactly on the final blow.
TEST(Game, FullFleetSunkDeclaresWinner) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    // Coordinates of every cell of Two's fleet (rows 0-4, standard layout).
    std::vector<Coord> two_cells = {
        // Carrier row 0, len 5
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0},
        // Battleship row 1, len 4
        {0, 1}, {1, 1}, {2, 1}, {3, 1},
        // Cruiser row 2, len 3
        {0, 2}, {1, 2}, {2, 2},
        // Submarine row 3, len 3
        {0, 3}, {1, 3}, {2, 3},
        // Destroyer row 4, len 2
        {0, 4}, {1, 4},
    };

    bool game_won = false;
    int arbitrary_counter_shot = 0;  // used to give Two harmless "return fire" cells
    for (size_t i = 0; i < two_cells.size(); ++i) {
        ShotResult result = game.shoot(PlayerId::One, two_cells[i]);
        EXPECT_TRUE(result.is_valid_input());
        if (i + 1 == two_cells.size()) {
            EXPECT_EQ(result.outcome, ShotOutcome::Sunk);
            EXPECT_TRUE(result.game_won);
            game_won = true;
        } else {
            EXPECT_NE(result.outcome, ShotOutcome::AlreadyShot);
            // Two takes a harmless turn back at an unoccupied, never-repeated
            // cell of One's board (One's fleet occupies rows 0-4, so rows 8-9
            // are empty and give 20 distinct cells -- more than the at most 16
            // counter-shots this loop needs).
            int x = arbitrary_counter_shot % kBoardSize;
            int y = 8 + (arbitrary_counter_shot / kBoardSize);
            ShotResult counter = game.shoot(PlayerId::Two, Coord(x, y));
            ASSERT_TRUE(counter.is_valid_input());
            ++arbitrary_counter_shot;
        }
    }

    ASSERT_TRUE(game_won);
    EXPECT_EQ(game.status(), MatchStatus::Finished);
    EXPECT_EQ(game.winner(), PlayerId::One);
    EXPECT_TRUE(game.board(PlayerId::Two).all_sunk());
}

TEST(Game, ShootAfterFinishedIsNoOp) {
    Game game;
    place_standard_fleet(game.board(PlayerId::One));
    place_standard_fleet(game.board(PlayerId::Two));
    game.start(PlayerId::One);

    std::vector<Coord> two_cells = {
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1},
        {0, 2}, {1, 2}, {2, 2}, {0, 3}, {1, 3}, {2, 3}, {0, 4}, {1, 4},
    };
    int counter = 0;
    for (size_t i = 0; i < two_cells.size(); ++i) {
        game.shoot(PlayerId::One, two_cells[i]);
        if (i + 1 != two_cells.size()) {
            int x = counter % kBoardSize;
            int y = 8 + (counter / kBoardSize);
            ShotResult counter_result = game.shoot(PlayerId::Two, Coord(x, y));
            ASSERT_TRUE(counter_result.is_valid_input());
            ++counter;
        }
    }
    ASSERT_EQ(game.status(), MatchStatus::Finished);

    ShotResult after = game.shoot(PlayerId::Two, Coord(5, 5));
    EXPECT_FALSE(after.game_won);
    EXPECT_EQ(game.status(), MatchStatus::Finished);
    EXPECT_EQ(game.winner(), PlayerId::One);
}

}  // namespace
