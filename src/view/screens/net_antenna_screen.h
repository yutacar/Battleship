/*
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

/// One-time LoRa antenna confirmation (Phase 8): shown when LoRa is picked
/// on the connection-type screen and the user has never confirmed that the
/// Cap LoRa-1262 antenna is attached (transmitting without it can damage the
/// Cap). Enter (the focused "Yes" button) persists the confirmation and the
/// LoRa preference and continues to the Host/Join submenu; short Esc goes
/// back to the connection-type screen without persisting anything (handled
/// centrally by ScreenManager::handle_short_esc).
class NetAntennaScreen : public BaseScreen {
public:
    NetAntennaScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen
