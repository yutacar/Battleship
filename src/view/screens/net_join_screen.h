/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Join Game screen (Phase 4): live list of LAN hosts discovered via UDP
/// broadcast (rebuilt whenever BaseViewModel bumps net_hosts_dirty), plus a
/// Direct IP fallback for networks where broadcast is filtered (PLAN.md §4)
/// and a Back button.
class NetJoinScreen : public BaseScreen {
public:
    NetJoinScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
