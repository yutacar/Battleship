/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/game.h"

#include "core/detail/snapshot_builder.h"

namespace battleship::core {

Board& Game::board(PlayerId player) { return boards_[index_of(player)]; }
const Board& Game::board(PlayerId player) const { return boards_[index_of(player)]; }

const TrackingBoard& Game::tracking_board(PlayerId player) const {
    return tracking_boards_[index_of(player)];
}

bool Game::start(PlayerId first_player) {
    if (status_ != MatchStatus::Placement) return false;
    if (!boards_[0].is_fully_placed() || !boards_[1].is_fully_placed()) return false;

    status_ = MatchStatus::InProgress;
    current_turn_ = first_player;
    turn_number_ = 0;
    return true;
}

ShotResult Game::shoot(PlayerId shooter, Coord target) {
    if (status_ != MatchStatus::InProgress || shooter != current_turn_) {
        ShotResult noop;
        noop.outcome = ShotOutcome::Miss;
        noop.game_won = false;
        return noop;
    }

    PlayerId opponent = other_player(shooter);
    Board& opponent_board = boards_[index_of(opponent)];
    ShotResult result = opponent_board.receive_shot(target);

    if (!result.is_valid_input()) return result;  // AlreadyShot/OutOfBounds: no state change

    TrackingBoard& shooter_tracking = tracking_boards_[index_of(shooter)];
    shooter_tracking.record_shot(target, result.outcome, result.sunk_ship);
    if (result.outcome == ShotOutcome::Sunk) {
        shooter_tracking.mark_ship_sunk(opponent_board.ship_cells(result.sunk_ship), result.sunk_ship);
    }

    ++turn_number_;

    if (result.game_won) {
        status_ = MatchStatus::Finished;
        winner_ = shooter;
    } else {
        current_turn_ = opponent;
    }

    return result;
}

MatchSnapshot Game::snapshot() const {
    MatchSnapshot snapshot;
    snapshot.status = status_;
    snapshot.current_turn = current_turn_;
    snapshot.winner = winner_;
    snapshot.turn_number = turn_number_;
    snapshot.player_one = detail::make_player_snapshot(boards_[0], tracking_boards_[0]);
    snapshot.player_two = detail::make_player_snapshot(boards_[1], tracking_boards_[1]);
    return snapshot;
}

}  // namespace battleship::core
