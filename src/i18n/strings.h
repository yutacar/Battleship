/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Lightweight self-rolled string catalog for Phase 5 (i18n). No gettext/ICU
 * dependency (PLAN.md "Phase 5（i18n）の設計方針"): every UI-facing string is
 * an entry in the StrId enum below, with one literal per language in
 * strings_en.cpp / strings_ja.cpp / strings_zh.cpp. Screens call tr(StrId)
 * (or trf(StrId, ...) for printf-style templates whose argument order may
 * need to differ per language) instead of embedding literals directly.
 *
 * Language switching: set_language() updates the process-global current
 * language and bumps language_subject() (an LVGL int subject) so screens
 * that want to react to a live language change can observe it; in practice
 * screens are cheap to fully rebuild (see ScreenManager::show_screen_for_page)
 * so BaseViewModel's set_language() simply re-shows the current page after
 * flipping the language, which re-runs build_content() and re-reads tr()
 * everywhere. Subject-bound dynamic labels (status line, commentary line,
 * net status line) are refreshed explicitly by BaseViewModel at the same
 * time since their text was formatted+cached, not re-read from a subject.
 */
#pragma once

#include <cstdarg>
#include <string>

#include "commentary/commentator.h"

namespace i18n {

/// Re-exported so callers outside commentary/ don't need to reach into that
/// namespace just to name a language. commentary::Lang is the canonical
/// definition (already plumbed through ICommentator; see PLAN.md Phase 5
/// notes in NOTES.md §10) -- i18n reuses it rather than defining a second,
/// parallel enum that could drift out of sync.
using Lang = battleship::commentary::Lang;

/// Every UI-facing string in the app. Grouped by screen/feature area in
/// declaration order purely for readability; the numeric values are never
/// persisted (only the selected Lang is), so reordering/inserting entries is
/// always safe as long as every language table in strings_<lang>.cpp is kept
/// in sync (test_i18n_catalog.cpp enforces this at test time).
enum class StrId {
    // ---- App / common ----
    AppTitle,
    ComingSoon,          ///< " (Coming soon)" suffix for disabled menu entries
    MenuQuitHint,        ///< title-screen hint: double Esc exits to the launcher
    MenuQuitArmed,       ///< shown for the double-Esc window after the first Esc
    Back,
    Cancel,
    Connect,

    // ---- Menu ----
    MenuTwoPlayerHotseat,
    MenuOnePlayer,
    MenuNetworkPlay,
    MenuSettings,

    // ---- Settings ----
    SettingsTitle,
    SettingsLanguageLabel,
    SettingsLanguageEnglish,
    SettingsLanguageJapanese,
    SettingsLanguageChinese,

    // ---- Difficulty ----
    DifficultyTitle,
    DifficultyEasyLabel,
    DifficultyEasyDesc,
    DifficultyNormalLabel,
    DifficultyNormalDesc,
    DifficultyHardLabel,
    DifficultyHardDesc,

    // ---- Ship names ----
    ShipCarrier,
    ShipBattleship,
    ShipCruiser,
    ShipSubmarine,
    ShipDestroyer,

    // ---- Placement ----
    PlacementPlayerN,             ///< "Player %d" (template, %d = 1 or 2)
    PlacementPlaceYourFleetSuffix, ///< ": place your fleet" appended to player label
    PlacementPlaceYourFleetSolo,  ///< "Place your fleet" (VsAi/Network, no player label)
    OrientationHorizontal,
    OrientationVertical,
    PlacementShipStatus,         ///< "%s (%s)" -- ship name + orientation
    PlacementFleetReady,
    PlacementConfirmNext,

    // ---- Handover ----
    HandoverPassDeviceTo,         ///< "Pass the device to %s"
    HandoverDontPeek,
    HandoverReady,

    // ---- Battle ----
    BattleTitleNet,
    BattleTitleVsCpu,
    BattleTitleP1Fire,
    BattleTitleP2Fire,
    BattleTargetsLabel,
    BattleYourFleetLabel,
    ShotMiss,
    ShotHit,
    ShotSunk,                    ///< "Sunk: %s!" (own kill) template
    ShotAlreadyTargeted,
    ShotSunkYours,                ///< "Sunk your %s!" (enemy sank yours) template
    BattleYouShotResult,          ///< "You %s: %s" template (coord + result fragment)
    BattleCpuShotResult,          ///< "CPU %s: %s" template (coord + result fragment)
    BattleVsCpuDifficulty,        ///< "vs CPU (%s)" template

    // ---- Result ----
    ResultYouWin,
    ResultCpuWins,
    ResultOpponentWins,
    ResultPlayer1Wins,
    ResultPlayer2Wins,
    ResultHitRateLineYou,          ///< "You: %d/%d shots (%.0f%%)"
    ResultHitRateLineCpu,          ///< "CPU: %d/%d shots (%.0f%%)"
    ResultHitRateLineOpponent,     ///< "Opponent: %d/%d shots (%.0f%%)"
    ResultHitRateLineP1,           ///< "P1: %d/%d shots (%.0f%%)"
    ResultHitRateLineP2,           ///< "P2: %d/%d shots (%.0f%%)"
    ResultTotalTurns,              ///< "Total turns: %d"
    ResultRematch,
    ResultTitle,

    // ---- Network menu ----
    NetworkPlayTitle,
    NetworkHostGame,
    NetworkJoinGame,

    // ---- Net lobby ----
    NetLobbyHostingTitle,
    NetLobbyJoiningTitle,
    NetLobbyWaitingForOpponent,
    NetLobbyWaitingForHost,
    NetCouldNotStartConnection,

    // ---- Net join ----
    NetJoinTitle,

    // ---- Net error ----
    NetErrorTitle,
    NetErrorOkBackToTitle,
    NetErrorConnectionLost,

    // ---- Net transport select (Phase 6) ----
    NetTransportTitle,             ///< "Connection Type" screen title
    NetTransportBle,               ///< "Bluetooth (BLE)" -- default, top row
    NetTransportLora,              ///< "LoRa" (Phase 8; selectable on every build)
    NetTransportDeviceOnly,        ///< " (device only)" suffix when BATTLESHIP_BLE=0
    NetTransportMethodLine,        ///< "Method: %s" hint on the Host/Join menu
    NetLobbyAdvertisingAs,         ///< "Advertising as %s ..." (BLE host lobby)
    NetLobbyConnectingToBle,       ///< "Connecting to %s ..." (BLE join, no port)
    NetJoinScanningBle,            ///< "Scanning for nearby hosts..." (BLE join)
    NetBleUnavailable,             ///< "Bluetooth unavailable" (adapter/bluetoothd missing)

    // ---- LoRa (Phase 8, Cap LoRa-1262 / desktop radio simulator) ----
    NetLoraHostingAs,              ///< "Recruiting over LoRa as %s ..." (LoRa host lobby)
    NetJoinScanningLora,           ///< "Listening for LoRa hosts..." (LoRa join)
    NetLoraCapMissing,             ///< Cap not detected (I/O expander / SX1262 silent)
    NetLoraUnavailable,            ///< any other radio start / link failure
    NetLoraAntennaQuestion,        ///< one-time antenna-attached confirmation
    NetLoraAntennaConfirm,         ///< "Enter: Yes   Esc: Back" key hint

    // ---- Net battle status ----
    NetYou,                        ///< "You" (shot-source label)
    NetFoe,                        ///< "Foe" (shot-source label)
    NetWaitingOpponentPlacing,
    NetYourTurnFire,
    NetShotFiredWaiting,
    NetWaitingOpponentAttack,
    NetVictory,
    NetDefeat,
    NetEnemySunkPrefix,            ///< "  Enemy sunk: " appended list prefix

    // ---- Esc-key convention (Phase 7, App Submission Guide) ----
    ConfirmAbandonTitle,           ///< "Abandon match?" interrupt-confirm modal
    ConfirmAbandonNetHint,         ///< "Your opponent will be disconnected."
    ConfirmYes,
    ConfirmNo,

    // ---- Battle live stats (mid-match score readout on the Battle screen) ----
    BattleStatsLine,               ///< "%s %d/%d (%.0f%%)" -- who label + hits/shots + hit rate
    BattleStatsSunkLine,           ///< "Sunk %d / Lost %d" -- enemy ships sunk / own ships lost

    // ---- Always-visible key guides (one short line per relevant screen; the
    // ---- %s in *Move/*Aim receives the LV_SYMBOL_* arrow cluster at trf()
    // ---- time so the catalog stays free of LVGL macros) ----
    KeyGuidePlacementMove,         ///< "%s:Move Enter:Place"
    KeyGuidePlacementModify,       ///< "R:Rotate X:Random"
    KeyGuidePlacementUndo,         ///< "BS:Undo Esc:Back"
    KeyGuideBattleAim,             ///< "%s:Aim Enter:Fire"
    KeyGuideBattleEsc,             ///< "Esc:Abandon"
    KeyGuideHandover,              ///< "Enter:Ready Esc:Abandon"

    kCount,  ///< sentinel -- number of entries, used by tests/tables only
};

constexpr std::size_t kStrIdCount = static_cast<std::size_t>(StrId::kCount);

/// Currently active output language for tr()/trf(). Defaults to English
/// (PLAN.md: "標準（未設定時）は英語"). Settings/persistence flips this at
/// startup after loading the saved preference (see i18n/settings.h).
Lang current_language();
void set_language(Lang lang);

/// Looks up the raw (unformatted) string for `id` in the current language.
/// For template entries (see StrId comments), the returned string still
/// contains printf-style placeholders -- use trf() to format them.
const char* tr(StrId id);

/// tr(id) formatted with printf-style varargs. Safe for the fixed set of
/// compile-time-literal templates in strings_<lang>.cpp; never pass
/// user-controlled data as the format string itself.
std::string trf(StrId id, ...);

/// Ship type name in the current language (Carrier/Battleship/...). Kept
/// separate from battleship::core::to_string(ShipType) (which stays
/// English-only for logs/tests) so UI call sites get localized names without
/// battleship_core needing any i18n dependency (it has none, by design).
std::string ship_name(battleship::core::ShipType type);

}  // namespace i18n
