/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * English string table -- the baseline language (PLAN.md: "英語を標準とし").
 * Every entry here is the literal text that shipped pre-Phase-5, verbatim,
 * so switching to English never changes existing screenshots/behavior.
 */
#include "i18n/strings_table.h"

namespace i18n::detail {

// clang-format off
const Table kEnglishTable = [] {
    Table t{};
    t[static_cast<std::size_t>(StrId::AppTitle)] = "Battleship";
    t[static_cast<std::size_t>(StrId::ComingSoon)] = " (Coming soon)";
    t[static_cast<std::size_t>(StrId::MenuQuitHint)] = "Esc x2: Quit";
    t[static_cast<std::size_t>(StrId::MenuQuitArmed)] = "Press Esc again to quit";
    t[static_cast<std::size_t>(StrId::Back)] = "Back";
    t[static_cast<std::size_t>(StrId::Cancel)] = "Cancel";
    t[static_cast<std::size_t>(StrId::Connect)] = "Connect";

    t[static_cast<std::size_t>(StrId::MenuTwoPlayerHotseat)] = "2 Player (Hotseat)";
    t[static_cast<std::size_t>(StrId::MenuOnePlayer)] = "1 Player";
    t[static_cast<std::size_t>(StrId::MenuNetworkPlay)] = "Network Play";
    t[static_cast<std::size_t>(StrId::MenuSettings)] = "Settings";

    t[static_cast<std::size_t>(StrId::SettingsTitle)] = "Settings";
    t[static_cast<std::size_t>(StrId::SettingsLanguageLabel)] = "Language";
    t[static_cast<std::size_t>(StrId::SettingsLanguageEnglish)] = "English";
    t[static_cast<std::size_t>(StrId::SettingsLanguageJapanese)] = "日本語";
    t[static_cast<std::size_t>(StrId::SettingsLanguageChinese)] = "简体中文";

    t[static_cast<std::size_t>(StrId::DifficultyTitle)] = "Select Difficulty";
    t[static_cast<std::size_t>(StrId::DifficultyEasyLabel)] = "Easy";
    t[static_cast<std::size_t>(StrId::DifficultyEasyDesc)] = "Random shots, chases hits";
    t[static_cast<std::size_t>(StrId::DifficultyNormalLabel)] = "Normal";
    t[static_cast<std::size_t>(StrId::DifficultyNormalDesc)] = "Smart targeting, some misses";
    t[static_cast<std::size_t>(StrId::DifficultyHardLabel)] = "Hard";
    t[static_cast<std::size_t>(StrId::DifficultyHardDesc)] = "Full probability hunting";

    t[static_cast<std::size_t>(StrId::ShipCarrier)] = "Carrier";
    t[static_cast<std::size_t>(StrId::ShipBattleship)] = "Battleship";
    t[static_cast<std::size_t>(StrId::ShipCruiser)] = "Cruiser";
    t[static_cast<std::size_t>(StrId::ShipSubmarine)] = "Submarine";
    t[static_cast<std::size_t>(StrId::ShipDestroyer)] = "Destroyer";

    t[static_cast<std::size_t>(StrId::PlacementPlayerN)] = "Player %d";
    t[static_cast<std::size_t>(StrId::PlacementPlaceYourFleetSuffix)] = ": place your fleet";
    t[static_cast<std::size_t>(StrId::PlacementPlaceYourFleetSolo)] = "Place your fleet";
    t[static_cast<std::size_t>(StrId::OrientationHorizontal)] = "Horizontal";
    t[static_cast<std::size_t>(StrId::OrientationVertical)] = "Vertical";
    t[static_cast<std::size_t>(StrId::PlacementShipStatus)] = "%s (%s)";
    t[static_cast<std::size_t>(StrId::PlacementFleetReady)] = "Fleet ready";
    t[static_cast<std::size_t>(StrId::PlacementConfirmNext)] = "Confirm -> Next";

    t[static_cast<std::size_t>(StrId::HandoverPassDeviceTo)] = "Pass the device to %s";
    t[static_cast<std::size_t>(StrId::HandoverDontPeek)] = "Don't peek at the other fleet!";
    t[static_cast<std::size_t>(StrId::HandoverReady)] = "Ready (Enter)";

    t[static_cast<std::size_t>(StrId::BattleTitleNet)] = "Net Battle";
    t[static_cast<std::size_t>(StrId::BattleTitleVsCpu)] = "You vs CPU";
    t[static_cast<std::size_t>(StrId::BattleTitleP1Fire)] = "P1 Fire!";
    t[static_cast<std::size_t>(StrId::BattleTitleP2Fire)] = "P2 Fire!";
    t[static_cast<std::size_t>(StrId::BattleTargetsLabel)] = "Targets";
    t[static_cast<std::size_t>(StrId::BattleYourFleetLabel)] = "Your Fleet";
    t[static_cast<std::size_t>(StrId::ShotMiss)] = "Miss";
    t[static_cast<std::size_t>(StrId::ShotHit)] = "Hit!";
    t[static_cast<std::size_t>(StrId::ShotSunk)] = "Sunk: %s!";
    t[static_cast<std::size_t>(StrId::ShotAlreadyTargeted)] = "Already targeted";
    t[static_cast<std::size_t>(StrId::ShotSunkYours)] = "Sunk your %s!";
    t[static_cast<std::size_t>(StrId::BattleYouShotResult)] = "You %s: %s";
    t[static_cast<std::size_t>(StrId::BattleCpuShotResult)] = "CPU %s: %s";
    t[static_cast<std::size_t>(StrId::BattleVsCpuDifficulty)] = "vs CPU (%s)";

    t[static_cast<std::size_t>(StrId::ResultYouWin)] = "You win!";
    t[static_cast<std::size_t>(StrId::ResultCpuWins)] = "CPU wins!";
    t[static_cast<std::size_t>(StrId::ResultOpponentWins)] = "Opponent wins!";
    t[static_cast<std::size_t>(StrId::ResultPlayer1Wins)] = "Player 1 wins!";
    t[static_cast<std::size_t>(StrId::ResultPlayer2Wins)] = "Player 2 wins!";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineYou)] = "You: %d/%d shots (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineCpu)] = "CPU: %d/%d shots (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineOpponent)] = "Opponent: %d/%d shots (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineP1)] = "P1: %d/%d shots (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineP2)] = "P2: %d/%d shots (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultTotalTurns)] = "Total turns: %d";
    t[static_cast<std::size_t>(StrId::ResultRematch)] = "Rematch";
    t[static_cast<std::size_t>(StrId::ResultTitle)] = "Title";

    t[static_cast<std::size_t>(StrId::NetworkPlayTitle)] = "Network Play";
    t[static_cast<std::size_t>(StrId::NetworkPlayHint)] = "Both players must be on the same WiFi network";
    t[static_cast<std::size_t>(StrId::NetworkHostGame)] = "Host Game";
    t[static_cast<std::size_t>(StrId::NetworkJoinGame)] = "Join Game";

    t[static_cast<std::size_t>(StrId::NetLobbyHostingTitle)] = "Hosting Game";
    t[static_cast<std::size_t>(StrId::NetLobbyJoiningTitle)] = "Joining Game";
    t[static_cast<std::size_t>(StrId::NetLobbyYourAddress)] = "Your address: %s:%u";
    t[static_cast<std::size_t>(StrId::NetLobbyConnectingTo)] = "Connecting to %s:%u ...";
    t[static_cast<std::size_t>(StrId::NetLobbyWaitingForOpponent)] = "Waiting for an opponent to join...";
    t[static_cast<std::size_t>(StrId::NetLobbyWaitingForHost)] = "Waiting for the host to respond...";
    t[static_cast<std::size_t>(StrId::NetCouldNotOpenPort)] = "Could not open a TCP port";
    t[static_cast<std::size_t>(StrId::NetCouldNotStartConnection)] = "Could not start connection";

    t[static_cast<std::size_t>(StrId::NetJoinTitle)] = "Join Game";
    t[static_cast<std::size_t>(StrId::NetJoinSearching)] = "Searching for hosts on your network...";
    t[static_cast<std::size_t>(StrId::NetJoinDiscoveryUnavailable)] = "Discovery unavailable - use Direct IP";
    t[static_cast<std::size_t>(StrId::NetDirectIpButton)] = "Direct IP";

    t[static_cast<std::size_t>(StrId::NetDirectIpTitle)] = "Direct IP";
    t[static_cast<std::size_t>(StrId::NetDirectIpHint)] = "Type the host's IP (port optional, ip:port)";
    t[static_cast<std::size_t>(StrId::NetDirectIpPlaceholder)] = "192.168.1.20";
    t[static_cast<std::size_t>(StrId::NetDirectIpInvalidAddress)] = "Invalid address - try e.g. 192.168.1.20";

    t[static_cast<std::size_t>(StrId::NetErrorTitle)] = "Network";
    t[static_cast<std::size_t>(StrId::NetErrorOkBackToTitle)] = "OK - back to title";
    t[static_cast<std::size_t>(StrId::NetErrorConnectionLost)] = "Connection lost";

    t[static_cast<std::size_t>(StrId::NetTransportTitle)] = "Connection Type";
    t[static_cast<std::size_t>(StrId::NetTransportBle)] = "Bluetooth (BLE)";
    t[static_cast<std::size_t>(StrId::NetTransportWifi)] = "WiFi (LAN)";
    t[static_cast<std::size_t>(StrId::NetTransportLora)] = "LoRa";
    t[static_cast<std::size_t>(StrId::NetTransportDeviceOnly)] = " (device only)";
    t[static_cast<std::size_t>(StrId::NetTransportMethodLine)] = "Method: %s";
    t[static_cast<std::size_t>(StrId::NetLobbyAdvertisingAs)] = "Advertising as %s ...";
    t[static_cast<std::size_t>(StrId::NetLobbyConnectingToBle)] = "Connecting to %s ...";
    t[static_cast<std::size_t>(StrId::NetJoinScanningBle)] = "Scanning for nearby hosts...";
    t[static_cast<std::size_t>(StrId::NetBleUnavailable)] = "Bluetooth unavailable";
    t[static_cast<std::size_t>(StrId::NetLoraHostingAs)] = "Recruiting over LoRa as %s ...";
    t[static_cast<std::size_t>(StrId::NetJoinScanningLora)] = "Listening for LoRa hosts...";
    t[static_cast<std::size_t>(StrId::NetLoraCapMissing)] = "Cap LoRa-1262 not found. Power off, attach it, then try again.";
    t[static_cast<std::size_t>(StrId::NetLoraUnavailable)] = "LoRa radio could not start.";
    t[static_cast<std::size_t>(StrId::NetLoraAntennaQuestion)] = "Is the antenna attached? Transmitting without it can damage the Cap.";
    t[static_cast<std::size_t>(StrId::NetLoraAntennaConfirm)] = "Enter: Yes   Esc: Back";

    t[static_cast<std::size_t>(StrId::NetYou)] = "You";
    t[static_cast<std::size_t>(StrId::NetFoe)] = "Foe";
    t[static_cast<std::size_t>(StrId::NetWaitingOpponentPlacing)] = "Waiting for opponent to place ships...";
    t[static_cast<std::size_t>(StrId::NetYourTurnFire)] = "Your turn - fire!";
    t[static_cast<std::size_t>(StrId::NetShotFiredWaiting)] = "Shot fired - waiting for result...";
    t[static_cast<std::size_t>(StrId::NetWaitingOpponentAttack)] = "Waiting for opponent's attack...";
    t[static_cast<std::size_t>(StrId::NetVictory)] = "Victory!";
    t[static_cast<std::size_t>(StrId::NetDefeat)] = "Defeat...";
    t[static_cast<std::size_t>(StrId::NetEnemySunkPrefix)] = "  Enemy sunk: ";
    t[static_cast<std::size_t>(StrId::ConfirmAbandonTitle)] = "Abandon match?";
    t[static_cast<std::size_t>(StrId::ConfirmAbandonNetHint)] = "Your opponent will be disconnected.";
    t[static_cast<std::size_t>(StrId::ConfirmYes)] = "Yes";
    t[static_cast<std::size_t>(StrId::ConfirmNo)] = "No";

    t[static_cast<std::size_t>(StrId::BattleStatsLine)] = "%s %d/%d (%.0f%%)";
    t[static_cast<std::size_t>(StrId::BattleStatsSunkLine)] = "Sunk %d / Lost %d";

    t[static_cast<std::size_t>(StrId::KeyGuidePlacementMove)] = "%s:Move  Enter:Place";
    t[static_cast<std::size_t>(StrId::KeyGuidePlacementModify)] = "R:Rotate  X:Random";
    t[static_cast<std::size_t>(StrId::KeyGuidePlacementUndo)] = "BS:Undo  Esc:Back";
    t[static_cast<std::size_t>(StrId::KeyGuideBattleAim)] = "%s:Aim  Enter:Fire";
    t[static_cast<std::size_t>(StrId::KeyGuideBattleEsc)] = "Esc:Abandon";
    t[static_cast<std::size_t>(StrId::KeyGuideHandover)] = "Enter:Ready  Esc:Abandon";
    return t;
}();
// clang-format on

}  // namespace i18n::detail
