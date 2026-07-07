/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Result screen: winner, both players' hit rate and shot count (computed
/// from Game::snapshot()). "Rematch" starts a fresh match (back to
/// Placement); "Title" returns to the main menu.
class ResultScreen : public BaseScreen {
public:
    ResultScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
