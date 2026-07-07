/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

/// Top-level UI flow states for the Battleship game.
/// Hotseat (Phase 2): Menu -> Placement (x2, one per player) -> Handover
/// (between placement and battle turns) -> Battle -> Result -> back to Menu.
/// 1-player vs AI (Phase 3a): Menu -> Difficulty -> Placement (human only)
/// -> Battle (no handovers; the AI fires automatically on its turn) ->
/// Result.
/// Network play (Phase 4; connection-type select added in Phase 6):
/// Menu -> NetTransport (BLE default / WiFi / LoRa disabled) -> NetworkMenu
/// -> (Host: NetLobby | Join: NetJoin [-> NetDirectIp, WiFi only] ->
/// NetLobby) -> Placement -> Battle -> Result; NetError interrupts from
/// anywhere on disconnect.
/// Settings (Phase 5, i18n): Menu -> Settings -> back to Menu. Language
/// switch takes effect immediately and rebuilds whatever screen is showing
/// (see BaseViewModel::set_language() / ScreenManager::reload_current_screen()).
enum class AppPage {
    Menu        = 0,
    Placement   = 1,
    Handover    = 2,
    Battle      = 3,
    Result      = 4,
    Difficulty  = 5,
    NetworkMenu = 6,  ///< Host / Join / Back submenu
    NetLobby    = 7,  ///< host: waiting for opponent; client: connecting
    NetJoin     = 8,  ///< discovered-host list (+ Direct IP fallback on WiFi)
    NetDirectIp = 9,  ///< manual IP[:port] entry (WiFi only)
    NetError    = 10, ///< "connection lost" dialog -> back to title
    Settings    = 11, ///< language selection (Phase 5)
    NetTransport = 12, ///< connection-type select: BLE (default) / WiFi / LoRa (Phase 6)
};

class BaseModel {
public:
    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

private:
    bool dark_mode_ = false;
    AppPage current_page_ = AppPage::Menu;
};

} // namespace model
