/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Join Game screen: live list of hosts found by the BLE scan or the LoRa
/// beacon scan (rebuilt whenever BaseViewModel bumps net_hosts_dirty) and a
/// Back button.
class NetJoinScreen : public BaseScreen {
public:
    NetJoinScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
