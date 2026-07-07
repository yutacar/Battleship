// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Board placement + shot resolution tests: bounds, overlap, one-per-type,
// hit/miss/sunk/already-shot outcomes, and full-fleet win detection.

#include "core/board.h"

#include <gtest/gtest.h>

using namespace battleship::core;

namespace {

TEST(Board, PlaceShipWithinBoundsSucceeds) {
    Board board;
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal)));
    ASSERT_EQ(board.ships().size(), 1u);
    EXPECT_EQ(board.ships()[0].type(), ShipType::Destroyer);
}

TEST(Board, PlaceShipOutOfBoundsHorizontalFails) {
    Board board;
    // Destroyer length 2, starting at x=9 would occupy x=9 and x=10 (out of bounds).
    EXPECT_FALSE(board.place_ship(Ship(ShipType::Destroyer, Coord(9, 0), Orientation::Horizontal)));
    EXPECT_TRUE(board.ships().empty());
}

TEST(Board, PlaceShipOutOfBoundsVerticalFails) {
    Board board;
    EXPECT_FALSE(board.place_ship(Ship(ShipType::Carrier, Coord(0, 9), Orientation::Vertical)));
    EXPECT_TRUE(board.ships().empty());
}

TEST(Board, PlaceShipWithNegativeOriginFails) {
    Board board;
    EXPECT_FALSE(board.place_ship(Ship(ShipType::Destroyer, Coord(-1, 0), Orientation::Horizontal)));
}

TEST(Board, OverlappingShipsRejected) {
    Board board;
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal)));
    // Cruiser vertical through (1,0) overlaps the destroyer's second cell (1,0).
    EXPECT_FALSE(board.place_ship(Ship(ShipType::Cruiser, Coord(1, 0), Orientation::Vertical)));
    EXPECT_EQ(board.ships().size(), 1u);
}

TEST(Board, AdjacentNonOverlappingShipsAllowed) {
    Board board;
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal)));
    // Touching edge-to-edge but not overlapping: destroyer occupies (0,0),(1,0).
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Cruiser, Coord(0, 1), Orientation::Horizontal)));
    EXPECT_EQ(board.ships().size(), 2u);
}

TEST(Board, DuplicateShipTypeRejected) {
    Board board;
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal)));
    EXPECT_FALSE(board.place_ship(Ship(ShipType::Destroyer, Coord(5, 5), Orientation::Horizontal)));
    EXPECT_EQ(board.ships().size(), 1u);
}

TEST(Board, IsFullyPlacedRequiresOneOfEachType) {
    Board board;
    EXPECT_FALSE(board.is_fully_placed());

    EXPECT_TRUE(board.place_ship(Ship(ShipType::Carrier, Coord(0, 0), Orientation::Horizontal)));
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Battleship, Coord(0, 1), Orientation::Horizontal)));
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Cruiser, Coord(0, 2), Orientation::Horizontal)));
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Submarine, Coord(0, 3), Orientation::Horizontal)));
    EXPECT_FALSE(board.is_fully_placed());

    EXPECT_TRUE(board.place_ship(Ship(ShipType::Destroyer, Coord(0, 4), Orientation::Horizontal)));
    EXPECT_TRUE(board.is_fully_placed());
}

TEST(Board, ClearResetsShipsAndShotHistory) {
    Board board;
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal));
    board.receive_shot(Coord(0, 0));
    board.clear();

    EXPECT_TRUE(board.ships().empty());
    EXPECT_FALSE(board.was_shot_at(Coord(0, 0)));
    EXPECT_FALSE(board.is_fully_placed());
}

TEST(Board, ShootMissOnEmptyCell) {
    Board board;
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal));
    ShotResult result = board.receive_shot(Coord(5, 5));
    EXPECT_EQ(result.outcome, ShotOutcome::Miss);
    EXPECT_FALSE(result.game_won);
}

TEST(Board, ShootHitOnPartialShip) {
    Board board;
    board.place_ship(Ship(ShipType::Cruiser, Coord(0, 0), Orientation::Horizontal));  // length 3
    ShotResult result = board.receive_shot(Coord(0, 0));
    EXPECT_EQ(result.outcome, ShotOutcome::Hit);
    EXPECT_FALSE(result.game_won);
}

TEST(Board, ShootSunkOnLastCellOnly) {
    Board board;
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal));  // length 2: (0,0),(1,0)

    ShotResult first = board.receive_shot(Coord(0, 0));
    EXPECT_EQ(first.outcome, ShotOutcome::Hit);

    ShotResult second = board.receive_shot(Coord(1, 0));
    EXPECT_EQ(second.outcome, ShotOutcome::Sunk);
    EXPECT_EQ(second.sunk_ship, ShipType::Destroyer);
}

TEST(Board, AlreadyShotCellRejectedWithoutStateChange) {
    Board board;
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal));
    board.receive_shot(Coord(0, 0));

    ShotResult repeat = board.receive_shot(Coord(0, 0));
    EXPECT_EQ(repeat.outcome, ShotOutcome::AlreadyShot);
    EXPECT_FALSE(repeat.is_valid_input());

    // remaining_cells for the destroyer should still show 1 (only one real hit
    // registered), proving the repeat shot did not alter state.
    EXPECT_EQ(board.remaining_cells(ShipType::Destroyer), 1);
}

TEST(Board, OutOfBoundsShotRejected) {
    Board board;
    ShotResult result = board.receive_shot(Coord(-1, 3));
    EXPECT_EQ(result.outcome, ShotOutcome::OutOfBounds);
    EXPECT_FALSE(result.is_valid_input());

    result = board.receive_shot(Coord(10, 3));
    EXPECT_EQ(result.outcome, ShotOutcome::OutOfBounds);

    result = board.receive_shot(Coord(3, -1));
    EXPECT_EQ(result.outcome, ShotOutcome::OutOfBounds);

    result = board.receive_shot(Coord(3, 10));
    EXPECT_EQ(result.outcome, ShotOutcome::OutOfBounds);
}

TEST(Board, ReportsCorrectShipTypeOnSunk) {
    Board board;
    board.place_ship(Ship(ShipType::Submarine, Coord(2, 2), Orientation::Vertical));  // length 3

    board.receive_shot(Coord(2, 2));
    board.receive_shot(Coord(2, 3));
    ShotResult result = board.receive_shot(Coord(2, 4));

    EXPECT_EQ(result.outcome, ShotOutcome::Sunk);
    EXPECT_EQ(result.sunk_ship, ShipType::Submarine);
}

// Places all five ships in non-overlapping rows and sinks all but the last
// cell of the last ship to verify game_won only flips on the final blow.
TEST(Board, AllSunkTriggersGameWonOnlyAfterFullFleetDown) {
    Board board;
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Carrier, Coord(0, 0), Orientation::Horizontal)));      // row 0, len 5
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Battleship, Coord(0, 1), Orientation::Horizontal)));   // row 1, len 4
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Cruiser, Coord(0, 2), Orientation::Horizontal)));      // row 2, len 3
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Submarine, Coord(0, 3), Orientation::Horizontal)));    // row 3, len 3
    EXPECT_TRUE(board.place_ship(Ship(ShipType::Destroyer, Coord(0, 4), Orientation::Horizontal)));    // row 4, len 2

    ASSERT_TRUE(board.is_fully_placed());

    // Sink everything except the destroyer's last cell.
    for (int x = 0; x < 5; ++x) board.receive_shot(Coord(x, 0));
    for (int x = 0; x < 4; ++x) board.receive_shot(Coord(x, 1));
    for (int x = 0; x < 3; ++x) board.receive_shot(Coord(x, 2));
    for (int x = 0; x < 3; ++x) board.receive_shot(Coord(x, 3));

    ShotResult penultimate = board.receive_shot(Coord(0, 4));
    EXPECT_EQ(penultimate.outcome, ShotOutcome::Hit);
    EXPECT_FALSE(penultimate.game_won);
    EXPECT_FALSE(board.all_sunk());

    ShotResult final_shot = board.receive_shot(Coord(1, 4));
    EXPECT_EQ(final_shot.outcome, ShotOutcome::Sunk);
    EXPECT_EQ(final_shot.sunk_ship, ShipType::Destroyer);
    EXPECT_TRUE(final_shot.game_won);
    EXPECT_TRUE(board.all_sunk());
}

TEST(Board, AllSunkFalseWhenNotFullyPlaced) {
    Board board;
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal));
    board.receive_shot(Coord(0, 0));
    board.receive_shot(Coord(1, 0));
    // Destroyer is sunk, but fleet isn't fully placed (only 1 of 5 types).
    EXPECT_FALSE(board.all_sunk());
}

TEST(Board, ShipCellsReturnsOccupiedCoordinates) {
    Board board;
    board.place_ship(Ship(ShipType::Cruiser, Coord(3, 4), Orientation::Horizontal));
    std::vector<Coord> cells = board.ship_cells(ShipType::Cruiser);
    ASSERT_EQ(cells.size(), 3u);
    EXPECT_EQ(cells[0], Coord(3, 4));
    EXPECT_EQ(cells[1], Coord(4, 4));
    EXPECT_EQ(cells[2], Coord(5, 4));
}

TEST(Board, ShipCellsEmptyForUnplacedType) {
    Board board;
    EXPECT_TRUE(board.ship_cells(ShipType::Carrier).empty());
}

TEST(Board, IsSunkFalseForUnplacedType) {
    Board board;
    EXPECT_FALSE(board.is_sunk(ShipType::Carrier));
}

}  // namespace
