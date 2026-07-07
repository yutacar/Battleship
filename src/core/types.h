/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Fundamental value types shared across the battleship::core library.
 * This header has zero dependencies beyond the standard library so it can be
 * used from GameCore, AI, tests, and (later) the UI/viewmodel layer alike.
 */
#pragma once

#include <cstdint>
#include <string>

namespace battleship::core {

/// Board dimension (10x10 classic Battleship grid).
constexpr int kBoardSize = 10;

/// A single grid coordinate. Valid range for both axes is [0, kBoardSize).
struct Coord {
    int x = 0;
    int y = 0;

    constexpr Coord() = default;
    constexpr Coord(int x_, int y_) : x(x_), y(y_) {}

    /// True when both axes fall within the 10x10 board.
    [[nodiscard]] constexpr bool in_bounds() const {
        return x >= 0 && x < kBoardSize && y >= 0 && y < kBoardSize;
    }

    constexpr bool operator==(const Coord& other) const {
        return x == other.x && y == other.y;
    }
    constexpr bool operator!=(const Coord& other) const { return !(*this == other); }
};

/// Ship placement direction. Ships occupy a straight run of cells; diagonal
/// placement is not representable by this type.
enum class Orientation {
    Horizontal,  ///< Occupies cells (x, y), (x+1, y), ...
    Vertical,    ///< Occupies cells (x, y), (x, y+1), ...
};

/// The five classic Battleship ship types, one of each per fleet.
enum class ShipType {
    Carrier,     ///< length 5
    Battleship,  ///< length 4
    Cruiser,     ///< length 3
    Submarine,   ///< length 3
    Destroyer,   ///< length 2
};

/// Number of distinct ship types in a fleet (one of each).
constexpr int kShipTypeCount = 5;

/// Returns the fixed hull length for a ship type.
constexpr int ship_length(ShipType type) {
    switch (type) {
        case ShipType::Carrier:
            return 5;
        case ShipType::Battleship:
            return 4;
        case ShipType::Cruiser:
            return 3;
        case ShipType::Submarine:
            return 3;
        case ShipType::Destroyer:
            return 2;
    }
    return 0;
}

/// Human-readable ship name, useful for UI labels and LLM commentary input.
std::string to_string(ShipType type);

/// All ship types in a stable, fixed order. Useful for iterating a fleet.
constexpr ShipType kAllShipTypes[kShipTypeCount] = {
    ShipType::Carrier, ShipType::Battleship, ShipType::Cruiser,
    ShipType::Submarine, ShipType::Destroyer,
};

/// Outcome of applying a single shot to a `Board`.
enum class ShotOutcome {
    Miss,          ///< Shot landed on an empty cell.
    Hit,           ///< Shot landed on a ship cell that was not the last afloat cell of that ship.
    Sunk,          ///< Shot hit the last remaining cell of a ship; see ShotResult::sunk_ship.
    AlreadyShot,   ///< Coordinate was already targeted before (invalid input, no state change).
    OutOfBounds,   ///< Coordinate is outside the board (invalid input, no state change).
};

/// Full result of a single shot, including whether it ended the game.
struct ShotResult {
    ShotOutcome outcome = ShotOutcome::Miss;
    /// Populated only when outcome == ShotOutcome::Sunk.
    ShipType sunk_ship = ShipType::Destroyer;
    /// True when this shot sank the last remaining ship of the target fleet.
    bool game_won = false;

    [[nodiscard]] constexpr bool is_valid_input() const {
        return outcome != ShotOutcome::AlreadyShot && outcome != ShotOutcome::OutOfBounds;
    }
};

}  // namespace battleship::core
