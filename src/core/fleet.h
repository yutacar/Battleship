/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Fleet placement helpers: random fleet generation for a Board. Used by the
 * UI's "random placement" action and by the AI's own board setup.
 */
#pragma once

#include <random>

#include "core/board.h"

namespace battleship::core {

/// Randomly places all kShipTypeCount ships onto `board` using uniform
/// rejection sampling: repeatedly pick a random origin + orientation for each
/// ship type and retry on collision until it fits. `board` is cleared first.
///
/// Deterministic given `rng`'s seed, so callers (including tests) can inject
/// a seeded `std::mt19937` for reproducible layouts.
///
/// This always succeeds for the classic 10x10 / 5-ship fleet (the board has
/// ample room), so no failure/retry-limit signaling is needed in practice;
/// it will simply keep retrying an individual ship placement until it lands.
void place_fleet_randomly(Board& board, std::mt19937& rng);

}  // namespace battleship::core
