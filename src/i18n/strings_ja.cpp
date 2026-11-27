/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Japanese string table. Deliberately terse (PLAN.md: "直訳より簡潔さ優先")
 * since 320x170 leaves little horizontal room and CJK glyphs are wider than
 * the Latin font metrics the layouts were tuned against.
 */
#include "i18n/strings_table.h"

namespace i18n::detail {

// clang-format off
const Table kJapaneseTable = [] {
    Table t{};
    t[static_cast<std::size_t>(StrId::AppTitle)] = "Battleship";
    t[static_cast<std::size_t>(StrId::ComingSoon)] = "（近日公開）";
    t[static_cast<std::size_t>(StrId::MenuQuitHint)] = "Esc x2: 終了";
    t[static_cast<std::size_t>(StrId::MenuQuitArmed)] = "もう一度 Esc で終了";
    t[static_cast<std::size_t>(StrId::Back)] = "戻る";
    t[static_cast<std::size_t>(StrId::Cancel)] = "キャンセル";
    t[static_cast<std::size_t>(StrId::Connect)] = "接続";

    t[static_cast<std::size_t>(StrId::MenuTwoPlayerHotseat)] = "2人対戦（交代制）";
    t[static_cast<std::size_t>(StrId::MenuOnePlayer)] = "1人対戦";
    t[static_cast<std::size_t>(StrId::MenuNetworkPlay)] = "通信対戦";
    t[static_cast<std::size_t>(StrId::MenuSettings)] = "設定";

    t[static_cast<std::size_t>(StrId::SettingsTitle)] = "設定";
    t[static_cast<std::size_t>(StrId::SettingsLanguageLabel)] = "言語";
    t[static_cast<std::size_t>(StrId::SettingsLanguageEnglish)] = "English";
    t[static_cast<std::size_t>(StrId::SettingsLanguageJapanese)] = "日本語";
    t[static_cast<std::size_t>(StrId::SettingsLanguageChinese)] = "简体中文";

    t[static_cast<std::size_t>(StrId::DifficultyTitle)] = "難易度を選択";
    t[static_cast<std::size_t>(StrId::DifficultyEasyLabel)] = "やさしい";
    t[static_cast<std::size_t>(StrId::DifficultyEasyDesc)] = "ランダム射撃・命中後は追撃";
    t[static_cast<std::size_t>(StrId::DifficultyNormalLabel)] = "ふつう";
    t[static_cast<std::size_t>(StrId::DifficultyNormalDesc)] = "的確な狙い・時々外す";
    t[static_cast<std::size_t>(StrId::DifficultyHardLabel)] = "むずかしい";
    t[static_cast<std::size_t>(StrId::DifficultyHardDesc)] = "確率計算でフル攻撃";

    t[static_cast<std::size_t>(StrId::ShipCarrier)] = "空母";
    t[static_cast<std::size_t>(StrId::ShipBattleship)] = "戦艦";
    t[static_cast<std::size_t>(StrId::ShipCruiser)] = "巡洋艦";
    t[static_cast<std::size_t>(StrId::ShipSubmarine)] = "潜水艦";
    t[static_cast<std::size_t>(StrId::ShipDestroyer)] = "駆逐艦";

    t[static_cast<std::size_t>(StrId::PlacementPlayerN)] = "プレイヤー%d";
    t[static_cast<std::size_t>(StrId::PlacementPlaceYourFleetSuffix)] = "：艦を配置";
    t[static_cast<std::size_t>(StrId::PlacementPlaceYourFleetSolo)] = "艦を配置してください";
    t[static_cast<std::size_t>(StrId::OrientationHorizontal)] = "横向き";
    t[static_cast<std::size_t>(StrId::OrientationVertical)] = "縦向き";
    t[static_cast<std::size_t>(StrId::PlacementShipStatus)] = "%s（%s）";
    t[static_cast<std::size_t>(StrId::PlacementFleetReady)] = "配置完了";
    t[static_cast<std::size_t>(StrId::PlacementConfirmNext)] = "確定 → 次へ";

    t[static_cast<std::size_t>(StrId::HandoverPassDeviceTo)] = "%sに交代してください";
    t[static_cast<std::size_t>(StrId::HandoverDontPeek)] = "相手の艦を見ないでください！";
    t[static_cast<std::size_t>(StrId::HandoverReady)] = "準備完了（Enter）";

    t[static_cast<std::size_t>(StrId::BattleTitleNet)] = "通信対戦";
    t[static_cast<std::size_t>(StrId::BattleTitleVsCpu)] = "vs CPU";
    t[static_cast<std::size_t>(StrId::BattleTitleP1Fire)] = "P1の攻撃！";
    t[static_cast<std::size_t>(StrId::BattleTitleP2Fire)] = "P2の攻撃！";
    t[static_cast<std::size_t>(StrId::BattleTargetsLabel)] = "攻撃盤";
    t[static_cast<std::size_t>(StrId::BattleYourFleetLabel)] = "自艦隊";
    t[static_cast<std::size_t>(StrId::ShotMiss)] = "はずれ";
    t[static_cast<std::size_t>(StrId::ShotHit)] = "命中！";
    t[static_cast<std::size_t>(StrId::ShotSunk)] = "撃沈：%s！";
    t[static_cast<std::size_t>(StrId::ShotAlreadyTargeted)] = "既に攻撃済み";
    t[static_cast<std::size_t>(StrId::ShotSunkYours)] = "%sが撃沈された！";
    t[static_cast<std::size_t>(StrId::BattleYouShotResult)] = "自分 %s: %s";
    t[static_cast<std::size_t>(StrId::BattleCpuShotResult)] = "CPU %s: %s";
    t[static_cast<std::size_t>(StrId::BattleVsCpuDifficulty)] = "vs CPU（%s）";

    t[static_cast<std::size_t>(StrId::ResultYouWin)] = "勝利！";
    t[static_cast<std::size_t>(StrId::ResultCpuWins)] = "CPUの勝ち";
    t[static_cast<std::size_t>(StrId::ResultOpponentWins)] = "相手の勝ち";
    t[static_cast<std::size_t>(StrId::ResultPlayer1Wins)] = "プレイヤー1の勝ち";
    t[static_cast<std::size_t>(StrId::ResultPlayer2Wins)] = "プレイヤー2の勝ち";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineYou)] = "自分: %d/%d発 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineCpu)] = "CPU: %d/%d発 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineOpponent)] = "相手: %d/%d発 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineP1)] = "P1: %d/%d発 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineP2)] = "P2: %d/%d発 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultTotalTurns)] = "総ターン数: %d";
    t[static_cast<std::size_t>(StrId::ResultRematch)] = "再戦";
    t[static_cast<std::size_t>(StrId::ResultTitle)] = "タイトルへ";

    t[static_cast<std::size_t>(StrId::NetworkPlayTitle)] = "通信対戦";
    t[static_cast<std::size_t>(StrId::NetworkPlayHint)] = "同じWiFiに接続してください";
    t[static_cast<std::size_t>(StrId::NetworkHostGame)] = "ホストする";
    t[static_cast<std::size_t>(StrId::NetworkJoinGame)] = "参加する";

    t[static_cast<std::size_t>(StrId::NetLobbyHostingTitle)] = "ホスト中";
    t[static_cast<std::size_t>(StrId::NetLobbyJoiningTitle)] = "接続中";
    t[static_cast<std::size_t>(StrId::NetLobbyYourAddress)] = "自分のアドレス: %s:%u";
    t[static_cast<std::size_t>(StrId::NetLobbyConnectingTo)] = "接続中 %s:%u ...";
    t[static_cast<std::size_t>(StrId::NetLobbyWaitingForOpponent)] = "対戦相手の参加を待っています...";
    t[static_cast<std::size_t>(StrId::NetLobbyWaitingForHost)] = "ホストの応答を待っています...";
    t[static_cast<std::size_t>(StrId::NetCouldNotOpenPort)] = "TCPポートを開けませんでした";
    t[static_cast<std::size_t>(StrId::NetCouldNotStartConnection)] = "接続を開始できませんでした";

    t[static_cast<std::size_t>(StrId::NetJoinTitle)] = "対戦に参加";
    t[static_cast<std::size_t>(StrId::NetJoinSearching)] = "ホストを検索しています...";
    t[static_cast<std::size_t>(StrId::NetJoinDiscoveryUnavailable)] = "検索不可 - IP直接入力を使用";
    t[static_cast<std::size_t>(StrId::NetDirectIpButton)] = "IP直接入力";

    t[static_cast<std::size_t>(StrId::NetDirectIpTitle)] = "IP直接入力";
    t[static_cast<std::size_t>(StrId::NetDirectIpHint)] = "ホストのIPを入力（ポート省略可）";
    t[static_cast<std::size_t>(StrId::NetDirectIpPlaceholder)] = "192.168.1.20";
    t[static_cast<std::size_t>(StrId::NetDirectIpInvalidAddress)] = "無効なアドレスです（例: 192.168.1.20）";

    t[static_cast<std::size_t>(StrId::NetErrorTitle)] = "通信エラー";
    t[static_cast<std::size_t>(StrId::NetErrorOkBackToTitle)] = "OK - タイトルへ";
    t[static_cast<std::size_t>(StrId::NetErrorConnectionLost)] = "接続が切断されました";

    t[static_cast<std::size_t>(StrId::NetTransportTitle)] = "接続方式";
    t[static_cast<std::size_t>(StrId::NetTransportBle)] = "Bluetooth (BLE)";
    t[static_cast<std::size_t>(StrId::NetTransportWifi)] = "WiFi (LAN)";
    t[static_cast<std::size_t>(StrId::NetTransportLora)] = "LoRa";
    t[static_cast<std::size_t>(StrId::NetTransportDeviceOnly)] = "（実機のみ）";
    t[static_cast<std::size_t>(StrId::NetTransportMethodLine)] = "接続方式: %s";
    t[static_cast<std::size_t>(StrId::NetLobbyAdvertisingAs)] = "%s として告知中...";
    t[static_cast<std::size_t>(StrId::NetLobbyConnectingToBle)] = "%s に接続しています...";
    t[static_cast<std::size_t>(StrId::NetJoinScanningBle)] = "近くのホストを検索しています...";
    t[static_cast<std::size_t>(StrId::NetBleUnavailable)] = "Bluetoothを利用できません";
    t[static_cast<std::size_t>(StrId::NetLoraHostingAs)] = "LoRa で募集中: %s ...";
    t[static_cast<std::size_t>(StrId::NetJoinScanningLora)] = "LoRa のホストを探しています...";
    t[static_cast<std::size_t>(StrId::NetLoraCapMissing)] = "Cap LoRa-1262 が見つかりません。電源を切って装着してから再試行してください。";
    t[static_cast<std::size_t>(StrId::NetLoraUnavailable)] = "LoRa 無線を開始できませんでした。";
    t[static_cast<std::size_t>(StrId::NetLoraAntennaQuestion)] = "アンテナを取り付けましたか？付けずに送信すると Cap が壊れることがあります。";
    t[static_cast<std::size_t>(StrId::NetLoraAntennaConfirm)] = "Enter: はい   Esc: 戻る";

    t[static_cast<std::size_t>(StrId::NetYou)] = "自分";
    t[static_cast<std::size_t>(StrId::NetFoe)] = "相手";
    t[static_cast<std::size_t>(StrId::NetWaitingOpponentPlacing)] = "相手の配置を待っています...";
    t[static_cast<std::size_t>(StrId::NetYourTurnFire)] = "あなたの番です！";
    t[static_cast<std::size_t>(StrId::NetShotFiredWaiting)] = "攻撃を送信、結果待ち...";
    t[static_cast<std::size_t>(StrId::NetWaitingOpponentAttack)] = "相手の攻撃を待っています...";
    t[static_cast<std::size_t>(StrId::NetVictory)] = "勝利！";
    t[static_cast<std::size_t>(StrId::NetDefeat)] = "敗北...";
    t[static_cast<std::size_t>(StrId::NetEnemySunkPrefix)] = "  撃沈: ";
    t[static_cast<std::size_t>(StrId::ConfirmAbandonTitle)] = "対戦を中断しますか？";
    t[static_cast<std::size_t>(StrId::ConfirmAbandonNetHint)] = "相手との接続は切断されます。";
    t[static_cast<std::size_t>(StrId::ConfirmYes)] = "はい";
    t[static_cast<std::size_t>(StrId::ConfirmNo)] = "いいえ";

    t[static_cast<std::size_t>(StrId::BattleStatsLine)] = "%s %d/%d (%.0f%%)";
    t[static_cast<std::size_t>(StrId::BattleStatsSunkLine)] = "撃沈%d / 損失%d";

    t[static_cast<std::size_t>(StrId::KeyGuidePlacementMove)] = "%s:移動  Enter:配置";
    t[static_cast<std::size_t>(StrId::KeyGuidePlacementModify)] = "R:回転  X:自動配置";
    t[static_cast<std::size_t>(StrId::KeyGuidePlacementUndo)] = "BS:取消  Esc:戻る";
    t[static_cast<std::size_t>(StrId::KeyGuideBattleAim)] = "%s:照準  Enter:射撃";
    t[static_cast<std::size_t>(StrId::KeyGuideBattleEsc)] = "Esc:中断";
    t[static_cast<std::size_t>(StrId::KeyGuideHandover)] = "Enter:開始  Esc:中断";
    return t;
}();
// clang-format on

}  // namespace i18n::detail
