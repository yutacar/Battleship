/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Tiny settings persistence for Phase 5. The imported template has no
 * config-file mechanism of its own (checked: src/config/ only holds LVGL
 * lv_conf*.h build-time headers, nothing runtime/user-facing -- see
 * NOTES.md Phase 5 notes), so this is a from-scratch key=value flat file,
 * intentionally minimal since the only setting today is the UI language.
 * Adding more keys later (e.g. commentary on/off, dark mode) is a matter of
 * adding another read/write pair here -- no format migration needed since
 * unknown keys are ignored and missing keys keep their caller-supplied
 * default.
 */
#pragma once

#include "i18n/strings.h"

namespace i18n {

/// Preferred 2-player connection method (Phase 6, PLAN.md §2.2 "マッチング
/// UI の接続方式選択"). Persisted alongside the language in the same
/// settings.conf ("transport=ble|wifi|lora") -- which is why it lives in this
/// settings module despite not being an i18n concern: the file is a single
/// flat key=value store and exactly one writer must own it (both save_*()
/// functions rewrite the whole file with every known key). Default is Ble --
/// BLE is the product's default connection method (PLAN.md Rev.3). On
/// builds without BLE support (BATTLESHIP_BLE=0, e.g. the macOS desktop
/// build) the UI treats Ble as "unavailable on this build" WITHOUT
/// rewriting the preference, so the same settings file still preselects BLE
/// on the device. Lora (Phase 8, "transport=lora") is available on every
/// build: the device drives the Cap LoRa-1262, the desktop build a UDP
/// loopback-multicast radio simulator (net/lora/radio_factory.h).
enum class NetTransportPref {
    Ble,
    Wifi,
    Lora,
};

/// Loads the persisted language (if any) and calls set_language() with it;
/// leaves the language at its current value (English, absent an earlier
/// set_language() call) if no settings file exists or it has no usable
/// "language=" entry -- this is how "未設定時は英語" (PLAN.md) is satisfied.
/// Also restores the persisted net-transport preference (default: Ble).
/// Safe to call once at startup, before building any UI.
void load_settings();

/// Persists `lang` (and calls set_language(lang)) so it survives a restart.
/// Called by the Settings screen the moment the user picks a language
/// (PLAN.md: "選択即反映" -- both live UI update AND persistence happen
/// together from BaseViewModel::set_language(), which is what actually
/// calls this).
void save_language(Lang lang);

/// Currently selected 2P connection method (in-memory; seeded by
/// load_settings(), Ble until then).
NetTransportPref net_transport_pref();

/// In-memory-only setter (no persistence) -- the transport counterpart of
/// set_language(). Used by tests to reset state between cases; UI code
/// should go through save_net_transport_pref() instead.
void set_net_transport_pref(NetTransportPref pref);

/// Persists `pref` (and applies it in-memory) so it survives a restart --
/// the connection-method screen calls this the moment the user picks one,
/// making the choice the preselected default next time (PLAN.md Phase 6:
/// "選択は設定に永続化（次回デフォルト）").
void save_net_transport_pref(NetTransportPref pref);

/// Whether the user has confirmed (once) that the Cap LoRa-1262 antenna is
/// attached -- transmitting without it can damage the Cap, so the LoRa flow
/// asks before the first use and never again once confirmed. Persisted as
/// "antenna_confirmed=1" (false/absent until then). In-memory value seeded
/// by load_settings().
bool antenna_confirmed();

/// In-memory-only setter (no persistence), for tests.
void set_antenna_confirmed(bool confirmed);

/// Persists `confirmed` (and applies it in-memory).
void save_antenna_confirmed(bool confirmed);

/// Path the settings file is read from / written to. Exposed for tests
/// (which override HOME/XDG_CONFIG_HOME via environment before calling)
/// and diagnostics; not needed by normal UI code.
std::string settings_file_path();

}  // namespace i18n
