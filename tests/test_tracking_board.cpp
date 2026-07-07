// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// TrackingBoard tests: cell marking, sunk-ship bookkeeping, win detection,
// and shot/hit counters used by snapshots.

#include "core/tracking_board.h"

#include <gtest/gtest.h>

using namespace battleship::core;

namespace {

TEST(TrackingBoard, InitiallyAllUnknown) {
    TrackingBoard tracking;
    EXPECT_EQ(tracking.mark_at(Coord(0, 0)), CellMark::Unknown);
    EXPECT_FALSE(tracking.was_shot_at(Coord(0, 0)));
    EXPECT_EQ(tracking.shots_fired(), 0);
    EXPECT_EQ(tracking.hits_recorded(), 0);
}

TEST(TrackingBoard, RecordMissSetsMarkAndCounters) {
    TrackingBoard tracking;
    EXPECT_TRUE(tracking.record_shot(Coord(2, 3), ShotOutcome::Miss));
    EXPECT_EQ(tracking.mark_at(Coord(2, 3)), CellMark::Miss);
    EXPECT_TRUE(tracking.was_shot_at(Coord(2, 3)));
    EXPECT_EQ(tracking.shots_fired(), 1);
    EXPECT_EQ(tracking.hits_recorded(), 0);
}

TEST(TrackingBoard, RecordHitSetsMarkAndIncrementsHits) {
    TrackingBoard tracking;
    EXPECT_TRUE(tracking.record_shot(Coord(2, 3), ShotOutcome::Hit));
    EXPECT_EQ(tracking.mark_at(Coord(2, 3)), CellMark::Hit);
    EXPECT_EQ(tracking.shots_fired(), 1);
    EXPECT_EQ(tracking.hits_recorded(), 1);
}

TEST(TrackingBoard, RecordSunkAddsToSunkShipsList) {
    TrackingBoard tracking;
    EXPECT_TRUE(tracking.record_shot(Coord(0, 0), ShotOutcome::Sunk, ShipType::Destroyer));
    EXPECT_EQ(tracking.mark_at(Coord(0, 0)), CellMark::Sunk);
    ASSERT_EQ(tracking.sunk_ships().size(), 1u);
    EXPECT_EQ(tracking.sunk_ships()[0], ShipType::Destroyer);
    EXPECT_EQ(tracking.hits_recorded(), 1);
}

TEST(TrackingBoard, RecordSunkDoesNotDuplicateShipInList) {
    TrackingBoard tracking;
    tracking.record_shot(Coord(0, 0), ShotOutcome::Sunk, ShipType::Destroyer);
    tracking.record_shot(Coord(1, 0), ShotOutcome::Sunk, ShipType::Destroyer);
    EXPECT_EQ(tracking.sunk_ships().size(), 1u);
}

TEST(TrackingBoard, RejectsAlreadyShotAndOutOfBoundsOutcomes) {
    TrackingBoard tracking;
    EXPECT_FALSE(tracking.record_shot(Coord(0, 0), ShotOutcome::AlreadyShot));
    EXPECT_FALSE(tracking.record_shot(Coord(0, 0), ShotOutcome::OutOfBounds));
    EXPECT_EQ(tracking.shots_fired(), 0);
    EXPECT_EQ(tracking.mark_at(Coord(0, 0)), CellMark::Unknown);
}

TEST(TrackingBoard, RejectsOutOfBoundsCoordinate) {
    TrackingBoard tracking;
    EXPECT_FALSE(tracking.record_shot(Coord(-1, 0), ShotOutcome::Miss));
    EXPECT_FALSE(tracking.record_shot(Coord(10, 0), ShotOutcome::Hit));
}

TEST(TrackingBoard, MarkShipSunkUpgradesAllHullCells) {
    TrackingBoard tracking;
    tracking.record_shot(Coord(0, 0), ShotOutcome::Hit);
    tracking.record_shot(Coord(1, 0), ShotOutcome::Sunk, ShipType::Destroyer);
    tracking.mark_ship_sunk({Coord(0, 0), Coord(1, 0)}, ShipType::Destroyer);

    EXPECT_EQ(tracking.mark_at(Coord(0, 0)), CellMark::Sunk);
    EXPECT_EQ(tracking.mark_at(Coord(1, 0)), CellMark::Sunk);
    ASSERT_EQ(tracking.sunk_ships().size(), 1u);
    EXPECT_EQ(tracking.sunk_ships()[0], ShipType::Destroyer);
}

TEST(TrackingBoard, RemainingShipTypesExcludesSunkOnes) {
    TrackingBoard tracking;
    tracking.record_shot(Coord(0, 0), ShotOutcome::Sunk, ShipType::Destroyer);
    std::vector<ShipType> remaining = tracking.remaining_ship_types();
    ASSERT_EQ(remaining.size(), static_cast<size_t>(kShipTypeCount - 1));
    for (ShipType type : remaining) {
        EXPECT_NE(type, ShipType::Destroyer);
    }
}

TEST(TrackingBoard, AllShipsSunkFalseUntilAllFiveConfirmed) {
    TrackingBoard tracking;
    for (ShipType type : {ShipType::Carrier, ShipType::Battleship, ShipType::Cruiser, ShipType::Submarine}) {
        EXPECT_FALSE(tracking.all_ships_sunk());
        tracking.record_shot(Coord(static_cast<int>(type), 0), ShotOutcome::Sunk, type);
    }
    EXPECT_FALSE(tracking.all_ships_sunk());
    tracking.record_shot(Coord(9, 9), ShotOutcome::Sunk, ShipType::Destroyer);
    EXPECT_TRUE(tracking.all_ships_sunk());
}

}  // namespace
