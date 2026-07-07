/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/player_view.h"

#include "core/detail/snapshot_builder.h"

namespace battleship::core {

bool PlayerView::start(bool goes_first) {
    if (status_ != MatchStatus::Placement) return false;
    if (!own_board_.is_fully_placed()) return false;

    status_ = MatchStatus::InProgress;
    is_my_turn_ = goes_first;
    return true;
}

ShotResult PlayerView::apply_incoming_shot(Coord target) {
    ShotResult result = own_board_.receive_shot(target);
    if (!result.is_valid_input()) return result;

    if (result.game_won) {
        status_ = MatchStatus::Finished;
        i_won_ = false;  // the opponent fired the winning shot against us
    } else {
        is_my_turn_ = true;  // opponent's turn is over, it's ours next
    }
    return result;
}

void PlayerView::apply_own_shot_result(Coord target, ShotResult result) {
    if (!result.is_valid_input()) return;

    tracking_board_.record_shot(target, result.outcome, result.sunk_ship);
    // We don't have the opponent's Board locally, so we can't enumerate the
    // sunk ship's other cells the way Game does. The final cell is enough to
    // register the ship as sunk in sunk_ships(); per-cell hull marking for
    // the other (already-Hit) cells of that ship is inherently unavailable
    // without opponent board knowledge, which is by design for networked play.

    if (result.game_won) {
        status_ = MatchStatus::Finished;
        i_won_ = true;
    } else {
        is_my_turn_ = false;  // our shot is resolved, opponent's turn next
    }
}

PlayerSnapshot PlayerView::snapshot() const {
    return detail::make_player_snapshot(own_board_, tracking_board_);
}

}  // namespace battleship::core
