/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// Connection-type select (Phase 6, PLAN.md §2.2 "マッチング UI の接続方式
/// 選択"): first screen of the Network Play flow. Rows, top to bottom:
///   - Bluetooth (BLE) -- the product default; preselected/checked per the
///     persisted preference. On builds without BLE (BATTLESHIP_BLE=0, e.g.
///     the macOS desktop build) the row is rendered disabled with a
///     "(device only)" suffix and WiFi becomes the effective choice.
///   - WiFi (LAN)      -- the Phase 4 flow, always available.
///   - LoRa            -- disabled "(Coming soon)" placeholder until Phase 8.
///   - Back            -- to the main menu.
/// Picking BLE or WiFi persists the choice (next-time default) and advances
/// to the Host/Join submenu.
class NetTransportScreen : public BaseScreen {
public:
    NetTransportScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
