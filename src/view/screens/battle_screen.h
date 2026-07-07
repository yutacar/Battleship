/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Battle screen: left = tracking board (calling shots at the opponent,
/// cursor-driven), right = own fleet board (own ships + damage taken).
/// Arrow keys move the tracking-board cursor, Enter calls a shot. Already
/// -shot cells are not selectable. Result (Hit/Miss/Sunk) shows in the
/// bottom status line (bound to view_model().status_text_subject()).
///
/// VsAi mode additionally shows a one-line commentator row (Phase 3b,
/// bound to view_model().commentary_text_subject()) directly above the
/// status line -- see build_content() for the exact layout budget. Hidden
/// entirely in Hotseat mode and while empty (no comment has landed yet),
/// so it never costs vertical space when commentary is disabled/unused.
class BattleScreen : public BaseScreen {
public:
    BattleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
