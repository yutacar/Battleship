/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Direct IP entry (Phase 4): fallback for networks where UDP broadcast
/// discovery is filtered (AP client isolation etc., PLAN.md §4). A one-line
/// text area accepts "192.168.1.20" or "192.168.1.20:35354" typed on the
/// 46-key keyboard (digits / dot / colon; on the CM0 device these are mapped
/// in platform/linux_input.cpp), then Connect starts the TCP join.
class NetDirectIpScreen : public BaseScreen {
public:
    NetDirectIpScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
