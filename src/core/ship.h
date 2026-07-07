/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Ship placement value type used by Board and fleet generation helpers.
 */
#pragma once

#include <array>

#include "core/types.h"

namespace battleship::core {

/// A placed ship: its type, origin cell, and orientation. The occupied cells
/// are derived (bow at `origin`, extending +x or +y for `ship_length` cells).
class Ship {
public:
    Ship() = default;
    Ship(ShipType type, Coord origin, Orientation orientation)
        : type_(type), origin_(origin), orientation_(orientation) {}

    [[nodiscard]] ShipType type() const { return type_; }
    [[nodiscard]] Coord origin() const { return origin_; }
    [[nodiscard]] Orientation orientation() const { return orientation_; }
    [[nodiscard]] int length() const { return ship_length(type_); }

    /// Returns the cell at `index` (0-based from the bow) along this ship's run.
    [[nodiscard]] Coord cell_at(int index) const {
        if (orientation_ == Orientation::Horizontal) {
            return Coord(origin_.x + index, origin_.y);
        }
        return Coord(origin_.x, origin_.y + index);
    }

    /// True when every cell of this ship lies within the board bounds.
    [[nodiscard]] bool fits_in_bounds() const {
        for (int i = 0; i < length(); ++i) {
            if (!cell_at(i).in_bounds()) return false;
        }
        return true;
    }

    /// True when `c` is one of this ship's occupied cells.
    [[nodiscard]] bool occupies(Coord c) const {
        for (int i = 0; i < length(); ++i) {
            if (cell_at(i) == c) return true;
        }
        return false;
    }

    /// True when this ship shares at least one cell with `other`.
    [[nodiscard]] bool overlaps(const Ship& other) const {
        for (int i = 0; i < length(); ++i) {
            if (other.occupies(cell_at(i))) return true;
        }
        return false;
    }

private:
    ShipType type_ = ShipType::Destroyer;
    Coord origin_;
    Orientation orientation_ = Orientation::Horizontal;
};

}  // namespace battleship::core
