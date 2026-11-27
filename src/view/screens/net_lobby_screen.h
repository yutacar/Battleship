/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Network lobby (Phase 4): shown while waiting for the connection to come
/// up. Host: "Waiting for opponent..." + the advertised name. Client:
/// "Connecting to ...".
/// Once the HELLO handshake completes, BaseViewModel's net poll timer
/// navigates to Placement automatically; Cancel tears the session down.
class NetLobbyScreen : public BaseScreen {
public:
    NetLobbyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
