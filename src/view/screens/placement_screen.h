/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Ship placement screen, shown once per player (view_model().placement_player()
/// tells us which). A 10x10 grid + a list of remaining ship types. Arrow keys
/// move the placement cursor, 'R' rotates the ship-to-place, Enter commits
/// it, 'X' randomizes the rest of the fleet, Backspace undoes the last
/// placed ship. Once the fleet is fully placed, a "Confirm -> Next" button
/// becomes available.
class PlacementScreen : public BaseScreen {
public:
    PlacementScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
