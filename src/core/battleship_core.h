/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Convenience umbrella header pulling in the full battleship::core public
 * API surface. Prefer including the specific headers you need in library
 * code; this is mainly for consumers (UI, AI, tests) that want everything.
 */
#pragma once

#include "core/ai.h"
#include "core/board.h"
#include "core/fleet.h"
#include "core/game.h"
#include "core/player_view.h"
#include "core/ship.h"
#include "core/snapshot.h"
#include "core/tracking_board.h"
#include "core/types.h"
