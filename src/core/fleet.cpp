/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/fleet.h"

namespace battleship::core {

void place_fleet_randomly(Board& board, std::mt19937& rng) {
    board.clear();

    std::uniform_int_distribution<int> coord_dist(0, kBoardSize - 1);
    std::uniform_int_distribution<int> orientation_dist(0, 1);

    for (ShipType type : kAllShipTypes) {
        while (true) {
            Coord origin(coord_dist(rng), coord_dist(rng));
            Orientation orientation =
                orientation_dist(rng) == 0 ? Orientation::Horizontal : Orientation::Vertical;
            Ship candidate(type, origin, orientation);
            if (board.place_ship(candidate)) break;
        }
    }
}

}  // namespace battleship::core
