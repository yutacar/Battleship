/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * TrackingBoard: records the history of shots fired AT an opponent, from the
 * shooter's point of view. It never holds opponent ship positions -- only
 * what has been revealed cell-by-cell via ShotResult. This is the input used
 * by the probability-density AI (Phase 3) and by the battle screen's
 * "tracking board" rendering (Phase 2), and is exactly what a network peer
 * needs to keep in sync during a remote match without ever seeing the
 * opponent's real board.
 */
#pragma once

#include <array>
#include <vector>

#include "core/types.h"

namespace battleship::core {

/// Per-cell knowledge state of a TrackingBoard.
enum class CellMark {
    Unknown,  ///< Not yet fired upon.
    Miss,     ///< Fired upon, no ship.
    Hit,      ///< Fired upon, hit a ship that is not yet fully sunk.
    Sunk,     ///< Fired upon, part of a ship that is now fully sunk.
};

/// Tracks the outcome of every shot fired at a single opponent board.
class TrackingBoard {
public:
    TrackingBoard() = default;

    /// Records the outcome of firing at `c`. `outcome` must be a *valid*
    /// input outcome (Miss/Hit/Sunk); AlreadyShot/OutOfBounds are rejected
    /// (returns false, no state change) since they carry no new information.
    /// When outcome is Sunk, `sunk_ship` identifies which ship type went
    /// down; every previously-Hit cell belonging to that ship is not
    /// retroactively updated by this call alone -- callers should also pass
    /// the full set of that ship's cells via `mark_ship_sunk` if precise
    /// per-cell Sunk marking is required (see below).
    bool record_shot(Coord c, ShotOutcome outcome, ShipType sunk_ship = ShipType::Destroyer);

    /// Retroactively upgrades every cell in `cells` (all previously Hit or
    /// about to be marked Hit) to CellMark::Sunk and associates them with
    /// `type`. Used when a Sunk result arrives so the whole hull -- not just
    /// the final cell -- is reflected as sunk.
    void mark_ship_sunk(const std::vector<Coord>& cells, ShipType type);

    /// Current knowledge state of cell `c`. Returns Unknown for out-of-bounds
    /// coordinates.
    [[nodiscard]] CellMark mark_at(Coord c) const;

    /// True when `c` has already been fired upon.
    [[nodiscard]] bool was_shot_at(Coord c) const;

    /// List of opponent ship types confirmed sunk so far (order = order in
    /// which they were sunk). Ship types NOT in this list are still afloat
    /// from this player's point of view (may be unplaced-unknown, in
    /// progress, or untouched).
    [[nodiscard]] const std::vector<ShipType>& sunk_ships() const { return sunk_ships_; }

    /// Convenience: ship types not yet confirmed sunk, in kAllShipTypes order.
    [[nodiscard]] std::vector<ShipType> remaining_ship_types() const;

    /// True when all kShipTypeCount ship types have been confirmed sunk,
    /// i.e. this player has won.
    [[nodiscard]] bool all_ships_sunk() const;

    /// Total shots recorded (Hit + Miss + Sunk cells), useful for hit-rate
    /// stats in snapshots/results.
    [[nodiscard]] int shots_fired() const { return shots_fired_; }
    [[nodiscard]] int hits_recorded() const { return hits_recorded_; }

private:
    std::array<std::array<CellMark, kBoardSize>, kBoardSize> marks_{};  ///< marks_[y][x]
    std::vector<ShipType> sunk_ships_;
    int shots_fired_ = 0;
    int hits_recorded_ = 0;
};

}  // namespace battleship::core
