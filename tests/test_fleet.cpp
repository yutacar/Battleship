// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Random fleet placement tests: validity across many seeds, determinism for
// a fixed seed, and full-fleet coverage.

#include "core/fleet.h"

#include <gtest/gtest.h>

#include <set>

using namespace battleship::core;

namespace {

// Captures the board's ships as a comparable snapshot (type/origin/orientation).
struct ShipLayout {
    ShipType type;
    Coord origin;
    Orientation orientation;

    bool operator==(const ShipLayout& other) const {
        return type == other.type && origin == other.origin && orientation == other.orientation;
    }
};

std::vector<ShipLayout> layout_of(const Board& board) {
    std::vector<ShipLayout> layout;
    for (const auto& ship : board.ships()) {
        layout.push_back({ship.type(), ship.origin(), ship.orientation()});
    }
    return layout;
}

bool no_overlaps(const Board& board) {
    const auto& ships = board.ships();
    for (size_t i = 0; i < ships.size(); ++i) {
        for (size_t j = i + 1; j < ships.size(); ++j) {
            if (ships[i].overlaps(ships[j])) return false;
        }
    }
    return true;
}

TEST(Fleet, RandomPlacementProducesFullyPlacedValidBoardManyTimes) {
    constexpr int kIterations = 1000;
    for (int seed = 0; seed < kIterations; ++seed) {
        std::mt19937 rng(static_cast<unsigned int>(seed));
        Board board;
        place_fleet_randomly(board, rng);

        ASSERT_TRUE(board.is_fully_placed()) << "seed=" << seed;
        ASSERT_EQ(board.ships().size(), static_cast<size_t>(kShipTypeCount)) << "seed=" << seed;
        ASSERT_TRUE(no_overlaps(board)) << "seed=" << seed;

        for (const auto& ship : board.ships()) {
            ASSERT_TRUE(ship.fits_in_bounds()) << "seed=" << seed;
        }

        // Exactly one of each ship type.
        std::set<ShipType> seen;
        for (const auto& ship : board.ships()) seen.insert(ship.type());
        ASSERT_EQ(seen.size(), static_cast<size_t>(kShipTypeCount)) << "seed=" << seed;
    }
}

TEST(Fleet, SameSeedProducesIdenticalLayout) {
    std::mt19937 rng1(42);
    Board board1;
    place_fleet_randomly(board1, rng1);

    std::mt19937 rng2(42);
    Board board2;
    place_fleet_randomly(board2, rng2);

    EXPECT_EQ(layout_of(board1), layout_of(board2));
}

TEST(Fleet, DifferentSeedsCanProduceDifferentLayouts) {
    // Not a strict guarantee for arbitrary RNGs, but with mt19937 and this
    // seed pair the layouts differ -- guards against a no-op/stubbed RNG use.
    std::mt19937 rng1(1);
    Board board1;
    place_fleet_randomly(board1, rng1);

    std::mt19937 rng2(2);
    Board board2;
    place_fleet_randomly(board2, rng2);

    EXPECT_NE(layout_of(board1), layout_of(board2));
}

TEST(Fleet, ClearsBoardBeforePlacing) {
    std::mt19937 rng(7);
    Board board;
    // Pre-existing manual placement should be wiped by place_fleet_randomly.
    board.place_ship(Ship(ShipType::Destroyer, Coord(0, 0), Orientation::Horizontal));
    place_fleet_randomly(board, rng);

    EXPECT_TRUE(board.is_fully_placed());
    EXPECT_EQ(board.ships().size(), static_cast<size_t>(kShipTypeCount));
}

}  // namespace
