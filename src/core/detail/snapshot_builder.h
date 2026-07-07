/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Internal helper shared by Game and PlayerView to build a PlayerSnapshot
 * from a Board + TrackingBoard pair. Not part of the public battleship::core
 * API surface (hence the `detail` namespace/directory); both call sites need
 * the exact same construction so snapshots stay consistent regardless of
 * which state-machine flavor produced them.
 */
#pragma once

#include "core/board.h"
#include "core/snapshot.h"
#include "core/tracking_board.h"

namespace battleship::core::detail {

inline std::vector<ShipType> own_ships_remaining(const Board& board) {
    std::vector<ShipType> remaining;
    for (ShipType type : kAllShipTypes) {
        if (!board.is_sunk(type)) remaining.push_back(type);
    }
    return remaining;
}

inline PlayerSnapshot make_player_snapshot(const Board& own_board, const TrackingBoard& tracking) {
    PlayerSnapshot snapshot;

    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            Coord c(x, y);
            int idx = y * kBoardSize + x;

            bool has_ship = false;
            for (const auto& ship : own_board.ships()) {
                if (ship.occupies(c)) {
                    has_ship = true;
                    break;
                }
            }
            snapshot.own_board[idx].has_ship = has_ship;
            snapshot.own_board[idx].was_hit = own_board.was_shot_at(c) && has_ship;
            snapshot.tracking_board[idx] = tracking.mark_at(c);
        }
    }

    snapshot.own_ships_remaining = own_ships_remaining(own_board);
    snapshot.enemy_ships_sunk = tracking.sunk_ships();
    snapshot.shots_fired = tracking.shots_fired();
    snapshot.hits_recorded = tracking.hits_recorded();
    return snapshot;
}

}  // namespace battleship::core::detail
