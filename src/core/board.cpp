/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/board.h"

namespace battleship::core {

bool Board::place_ship(const Ship& ship) {
    if (!ship.fits_in_bounds()) return false;

    for (const auto& existing : ships_) {
        if (existing.type() == ship.type()) return false;  // one of each type only
        if (existing.overlaps(ship)) return false;
    }

    ships_.push_back(ship);
    return true;
}

void Board::clear() {
    ships_.clear();
    for (auto& row : shot_at_) row.fill(false);
    for (auto& row : hit_) row.fill(false);
}

bool Board::is_fully_placed() const {
    return ships_.size() == static_cast<size_t>(kShipTypeCount);
}

const Ship* Board::find_ship_at(Coord c) const {
    for (const auto& ship : ships_) {
        if (ship.occupies(c)) return &ship;
    }
    return nullptr;
}

const Ship* Board::find_ship(ShipType type) const {
    for (const auto& ship : ships_) {
        if (ship.type() == type) return &ship;
    }
    return nullptr;
}

ShotResult Board::receive_shot(Coord c) {
    ShotResult result;

    if (!c.in_bounds()) {
        result.outcome = ShotOutcome::OutOfBounds;
        return result;
    }
    if (shot_at_[c.y][c.x]) {
        result.outcome = ShotOutcome::AlreadyShot;
        return result;
    }

    shot_at_[c.y][c.x] = true;

    const Ship* ship = find_ship_at(c);
    if (ship == nullptr) {
        result.outcome = ShotOutcome::Miss;
        return result;
    }

    hit_[c.y][c.x] = true;

    if (remaining_cells(ship->type()) == 0) {
        result.outcome = ShotOutcome::Sunk;
        result.sunk_ship = ship->type();
        result.game_won = all_sunk();
    } else {
        result.outcome = ShotOutcome::Hit;
    }
    return result;
}

bool Board::was_shot_at(Coord c) const {
    if (!c.in_bounds()) return false;
    return shot_at_[c.y][c.x];
}

int Board::remaining_cells(ShipType type) const {
    const Ship* ship = find_ship(type);
    if (ship == nullptr) return 0;

    int remaining = 0;
    for (int i = 0; i < ship->length(); ++i) {
        Coord cell = ship->cell_at(i);
        if (!hit_[cell.y][cell.x]) ++remaining;
    }
    return remaining;
}

bool Board::is_sunk(ShipType type) const {
    const Ship* ship = find_ship(type);
    if (ship == nullptr) return false;
    return remaining_cells(type) == 0;
}

std::vector<Coord> Board::ship_cells(ShipType type) const {
    std::vector<Coord> cells;
    const Ship* ship = find_ship(type);
    if (ship == nullptr) return cells;

    cells.reserve(static_cast<size_t>(ship->length()));
    for (int i = 0; i < ship->length(); ++i) {
        cells.push_back(ship->cell_at(i));
    }
    return cells;
}

bool Board::all_sunk() const {
    if (!is_fully_placed()) return false;
    for (const auto& ship : ships_) {
        if (remaining_cells(ship.type()) != 0) return false;
    }
    return true;
}

}  // namespace battleship::core
