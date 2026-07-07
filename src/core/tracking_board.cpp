/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/tracking_board.h"

#include <algorithm>

namespace battleship::core {

bool TrackingBoard::record_shot(Coord c, ShotOutcome outcome, ShipType sunk_ship) {
    if (!c.in_bounds()) return false;
    if (outcome == ShotOutcome::AlreadyShot || outcome == ShotOutcome::OutOfBounds) return false;

    switch (outcome) {
        case ShotOutcome::Miss:
            marks_[c.y][c.x] = CellMark::Miss;
            break;
        case ShotOutcome::Hit:
            marks_[c.y][c.x] = CellMark::Hit;
            ++hits_recorded_;
            break;
        case ShotOutcome::Sunk:
            marks_[c.y][c.x] = CellMark::Sunk;
            ++hits_recorded_;
            if (std::find(sunk_ships_.begin(), sunk_ships_.end(), sunk_ship) == sunk_ships_.end()) {
                sunk_ships_.push_back(sunk_ship);
            }
            break;
        default:
            break;
    }

    ++shots_fired_;
    return true;
}

void TrackingBoard::mark_ship_sunk(const std::vector<Coord>& cells, ShipType type) {
    for (const Coord& c : cells) {
        if (!c.in_bounds()) continue;
        marks_[c.y][c.x] = CellMark::Sunk;
    }
    if (std::find(sunk_ships_.begin(), sunk_ships_.end(), type) == sunk_ships_.end()) {
        sunk_ships_.push_back(type);
    }
}

CellMark TrackingBoard::mark_at(Coord c) const {
    if (!c.in_bounds()) return CellMark::Unknown;
    return marks_[c.y][c.x];
}

bool TrackingBoard::was_shot_at(Coord c) const {
    return mark_at(c) != CellMark::Unknown;
}

std::vector<ShipType> TrackingBoard::remaining_ship_types() const {
    std::vector<ShipType> remaining;
    for (ShipType type : kAllShipTypes) {
        if (std::find(sunk_ships_.begin(), sunk_ships_.end(), type) == sunk_ships_.end()) {
            remaining.push_back(type);
        }
    }
    return remaining;
}

bool TrackingBoard::all_ships_sunk() const {
    return sunk_ships_.size() == static_cast<size_t>(kShipTypeCount);
}

}  // namespace battleship::core
