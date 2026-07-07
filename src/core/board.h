/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Board: a player's own 10x10 fleet grid. Owns ship placement validation,
 * incoming-shot resolution, and per-ship/overall sunk-state bookkeeping.
 */
#pragma once

#include <array>
#include <vector>

#include "core/ship.h"
#include "core/types.h"

namespace battleship::core {

/// A player's own fleet board: placement validation + incoming shot resolution.
///
/// `Board` represents ground truth for one player's ships. It never leaks
/// unsunk ship positions through its public API beyond what `ShotResult`
/// reveals for the specific cell shot at -- callers that need to reveal a
/// full board (e.g. a "reveal fleet" result screen) can use `ships()`
/// explicitly once the match is over.
class Board {
public:
    Board() = default;

    /// Attempts to place `ship`. Fails (returns false, no state change) if the
    /// ship falls outside the board, overlaps a previously placed ship, or if
    /// a ship of the same type has already been placed. On success the ship
    /// is added and true is returned.
    bool place_ship(const Ship& ship);

    /// Removes all placed ships and shot history, resetting to an empty board.
    void clear();

    /// True once one ship of every `ShipType` has been placed.
    [[nodiscard]] bool is_fully_placed() const;

    /// Read-only access to placed ships (order of placement).
    [[nodiscard]] const std::vector<Ship>& ships() const { return ships_; }

    /// Applies an incoming shot at `c` to this board (i.e. the opponent is
    /// firing at us). Idempotent guard: repeat shots at the same cell return
    /// ShotOutcome::AlreadyShot without altering state. Out-of-bounds
    /// coordinates return ShotOutcome::OutOfBounds without altering state.
    ShotResult receive_shot(Coord c);

    /// True when `c` has already been targeted (hit or miss) on this board.
    [[nodiscard]] bool was_shot_at(Coord c) const;

    /// True when every placed ship is fully sunk (all fleet cells hit).
    /// Only meaningful once is_fully_placed() is true.
    [[nodiscard]] bool all_sunk() const;

    /// Number of cells of `type` that have NOT been hit yet. Returns 0 if the
    /// ship type has not been placed.
    [[nodiscard]] int remaining_cells(ShipType type) const;

    /// True when the ship of `type` is placed and fully sunk.
    [[nodiscard]] bool is_sunk(ShipType type) const;

    /// Returns all cells occupied by the ship of `type`, or an empty vector
    /// if that type has not been placed. Useful for callers (e.g. Game) that
    /// need to mark a TrackingBoard's cells as Sunk once a ship goes down.
    [[nodiscard]] std::vector<Coord> ship_cells(ShipType type) const;

private:
    [[nodiscard]] const Ship* find_ship_at(Coord c) const;
    [[nodiscard]] const Ship* find_ship(ShipType type) const;

    std::vector<Ship> ships_;
    std::array<std::array<bool, kBoardSize>, kBoardSize> shot_at_{};  ///< shot_at_[y][x]
    std::array<std::array<bool, kBoardSize>, kBoardSize> hit_{};      ///< hit_[y][x], subset of shot_at_
};

}  // namespace battleship::core
