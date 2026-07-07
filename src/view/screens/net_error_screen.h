/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Network error dialog page (Phase 4): shown when the connection is lost
/// mid-flow (PING silence, retransmit exhaustion, peer closed) or a network
/// resource could not be opened. Displays the message from
/// net_status_subject() ("Connection lost", ...) and returns to the title
/// on OK (which also tears the network session down, see
/// BaseViewModel::show_page).
class NetErrorScreen : public BaseScreen {
public:
    NetErrorScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
