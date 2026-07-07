/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Simplified Chinese string table. Kept terse for the same layout-budget
 * reasons as strings_ja.cpp (PLAN.md: "直訳より簡潔さ優先", 320x170).
 */
#include "i18n/strings_table.h"

namespace i18n::detail {

// clang-format off
const Table kChineseTable = [] {
    Table t{};
    t[static_cast<std::size_t>(StrId::AppTitle)] = "Battleship";
    t[static_cast<std::size_t>(StrId::ComingSoon)] = "（即将推出）";
    t[static_cast<std::size_t>(StrId::Back)] = "返回";
    t[static_cast<std::size_t>(StrId::Cancel)] = "取消";
    t[static_cast<std::size_t>(StrId::Connect)] = "连接";

    t[static_cast<std::size_t>(StrId::MenuTwoPlayerHotseat)] = "双人对战（轮流）";
    t[static_cast<std::size_t>(StrId::MenuOnePlayer)] = "单人对战";
    t[static_cast<std::size_t>(StrId::MenuNetworkPlay)] = "联网对战";
    t[static_cast<std::size_t>(StrId::MenuSettings)] = "设置";

    t[static_cast<std::size_t>(StrId::SettingsTitle)] = "设置";
    t[static_cast<std::size_t>(StrId::SettingsLanguageLabel)] = "语言";
    t[static_cast<std::size_t>(StrId::SettingsLanguageEnglish)] = "English";
    t[static_cast<std::size_t>(StrId::SettingsLanguageJapanese)] = "日本語";
    t[static_cast<std::size_t>(StrId::SettingsLanguageChinese)] = "简体中文";

    t[static_cast<std::size_t>(StrId::DifficultyTitle)] = "选择难度";
    t[static_cast<std::size_t>(StrId::DifficultyEasyLabel)] = "简单";
    t[static_cast<std::size_t>(StrId::DifficultyEasyDesc)] = "随机射击，命中后追击";
    t[static_cast<std::size_t>(StrId::DifficultyNormalLabel)] = "普通";
    t[static_cast<std::size_t>(StrId::DifficultyNormalDesc)] = "精准瞄准，偶尔失误";
    t[static_cast<std::size_t>(StrId::DifficultyHardLabel)] = "困难";
    t[static_cast<std::size_t>(StrId::DifficultyHardDesc)] = "概率计算全力搜索";

    t[static_cast<std::size_t>(StrId::ShipCarrier)] = "航母";
    t[static_cast<std::size_t>(StrId::ShipBattleship)] = "战舰";
    t[static_cast<std::size_t>(StrId::ShipCruiser)] = "巡洋舰";
    t[static_cast<std::size_t>(StrId::ShipSubmarine)] = "潜艇";
    t[static_cast<std::size_t>(StrId::ShipDestroyer)] = "驱逐舰";

    t[static_cast<std::size_t>(StrId::PlacementPlayerN)] = "玩家%d";
    t[static_cast<std::size_t>(StrId::PlacementPlaceYourFleetSuffix)] = "：布置舰队";
    t[static_cast<std::size_t>(StrId::PlacementPlaceYourFleetSolo)] = "请布置你的舰队";
    t[static_cast<std::size_t>(StrId::OrientationHorizontal)] = "横向";
    t[static_cast<std::size_t>(StrId::OrientationVertical)] = "纵向";
    t[static_cast<std::size_t>(StrId::PlacementShipStatus)] = "%s（%s）";
    t[static_cast<std::size_t>(StrId::PlacementFleetReady)] = "布置完成";
    t[static_cast<std::size_t>(StrId::PlacementConfirmNext)] = "确认 → 下一步";

    t[static_cast<std::size_t>(StrId::HandoverPassDeviceTo)] = "请将设备交给%s";
    t[static_cast<std::size_t>(StrId::HandoverDontPeek)] = "请勿偷看对方的舰队！";
    t[static_cast<std::size_t>(StrId::HandoverReady)] = "准备好了（Enter）";

    t[static_cast<std::size_t>(StrId::BattleTitleNet)] = "联网对战";
    t[static_cast<std::size_t>(StrId::BattleTitleVsCpu)] = "对战 CPU";
    t[static_cast<std::size_t>(StrId::BattleTitleP1Fire)] = "P1 开火！";
    t[static_cast<std::size_t>(StrId::BattleTitleP2Fire)] = "P2 开火！";
    t[static_cast<std::size_t>(StrId::BattleTargetsLabel)] = "攻击盘";
    t[static_cast<std::size_t>(StrId::BattleYourFleetLabel)] = "我方舰队";
    t[static_cast<std::size_t>(StrId::ShotMiss)] = "未命中";
    t[static_cast<std::size_t>(StrId::ShotHit)] = "命中！";
    t[static_cast<std::size_t>(StrId::ShotSunk)] = "击沉：%s！";
    t[static_cast<std::size_t>(StrId::ShotAlreadyTargeted)] = "已攻击过";
    t[static_cast<std::size_t>(StrId::ShotSunkYours)] = "你的%s被击沉！";
    t[static_cast<std::size_t>(StrId::BattleYouShotResult)] = "你 %s: %s";
    t[static_cast<std::size_t>(StrId::BattleCpuShotResult)] = "CPU %s: %s";
    t[static_cast<std::size_t>(StrId::BattleVsCpuDifficulty)] = "对战 CPU（%s）";

    t[static_cast<std::size_t>(StrId::ResultYouWin)] = "你赢了！";
    t[static_cast<std::size_t>(StrId::ResultCpuWins)] = "CPU 获胜";
    t[static_cast<std::size_t>(StrId::ResultOpponentWins)] = "对方获胜";
    t[static_cast<std::size_t>(StrId::ResultPlayer1Wins)] = "玩家1获胜";
    t[static_cast<std::size_t>(StrId::ResultPlayer2Wins)] = "玩家2获胜";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineYou)] = "你: %d/%d发 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineCpu)] = "CPU: %d/%d发 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineOpponent)] = "对方: %d/%d发 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineP1)] = "P1: %d/%d发 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultHitRateLineP2)] = "P2: %d/%d发 (%.0f%%)";
    t[static_cast<std::size_t>(StrId::ResultTotalTurns)] = "总回合数: %d";
    t[static_cast<std::size_t>(StrId::ResultRematch)] = "再来一局";
    t[static_cast<std::size_t>(StrId::ResultTitle)] = "返回标题";

    t[static_cast<std::size_t>(StrId::NetworkPlayTitle)] = "联网对战";
    t[static_cast<std::size_t>(StrId::NetworkPlayHint)] = "双方需连接同一WiFi网络";
    t[static_cast<std::size_t>(StrId::NetworkHostGame)] = "创建主机";
    t[static_cast<std::size_t>(StrId::NetworkJoinGame)] = "加入对战";

    t[static_cast<std::size_t>(StrId::NetLobbyHostingTitle)] = "主机中";
    t[static_cast<std::size_t>(StrId::NetLobbyJoiningTitle)] = "连接中";
    t[static_cast<std::size_t>(StrId::NetLobbyYourAddress)] = "你的地址: %s:%u";
    t[static_cast<std::size_t>(StrId::NetLobbyConnectingTo)] = "正在连接 %s:%u ...";
    t[static_cast<std::size_t>(StrId::NetLobbyWaitingForOpponent)] = "等待对手加入...";
    t[static_cast<std::size_t>(StrId::NetLobbyWaitingForHost)] = "等待主机响应...";
    t[static_cast<std::size_t>(StrId::NetCouldNotOpenPort)] = "无法打开TCP端口";
    t[static_cast<std::size_t>(StrId::NetCouldNotStartConnection)] = "无法建立连接";

    t[static_cast<std::size_t>(StrId::NetJoinTitle)] = "加入对战";
    t[static_cast<std::size_t>(StrId::NetJoinSearching)] = "正在搜索主机...";
    t[static_cast<std::size_t>(StrId::NetJoinDiscoveryUnavailable)] = "搜索不可用 - 请用IP直接输入";
    t[static_cast<std::size_t>(StrId::NetDirectIpButton)] = "IP直接输入";

    t[static_cast<std::size_t>(StrId::NetDirectIpTitle)] = "IP直接输入";
    t[static_cast<std::size_t>(StrId::NetDirectIpHint)] = "输入主机IP（端口可选）";
    t[static_cast<std::size_t>(StrId::NetDirectIpPlaceholder)] = "192.168.1.20";
    t[static_cast<std::size_t>(StrId::NetDirectIpInvalidAddress)] = "地址无效（例如 192.168.1.20）";

    t[static_cast<std::size_t>(StrId::NetErrorTitle)] = "网络错误";
    t[static_cast<std::size_t>(StrId::NetErrorOkBackToTitle)] = "确定 - 返回标题";
    t[static_cast<std::size_t>(StrId::NetErrorConnectionLost)] = "连接已断开";

    t[static_cast<std::size_t>(StrId::NetTransportTitle)] = "连接方式";
    t[static_cast<std::size_t>(StrId::NetTransportBle)] = "蓝牙 (BLE)";
    t[static_cast<std::size_t>(StrId::NetTransportWifi)] = "WiFi (LAN)";
    t[static_cast<std::size_t>(StrId::NetTransportLora)] = "LoRa";
    t[static_cast<std::size_t>(StrId::NetTransportDeviceOnly)] = "（仅实机）";
    t[static_cast<std::size_t>(StrId::NetTransportMethodLine)] = "连接方式：%s";
    t[static_cast<std::size_t>(StrId::NetLobbyAdvertisingAs)] = "正在以 %s 广播...";
    t[static_cast<std::size_t>(StrId::NetLobbyConnectingToBle)] = "正在连接 %s ...";
    t[static_cast<std::size_t>(StrId::NetJoinScanningBle)] = "正在扫描附近的主机...";
    t[static_cast<std::size_t>(StrId::NetBleUnavailable)] = "蓝牙不可用";

    t[static_cast<std::size_t>(StrId::NetYou)] = "你";
    t[static_cast<std::size_t>(StrId::NetFoe)] = "对方";
    t[static_cast<std::size_t>(StrId::NetWaitingOpponentPlacing)] = "等待对方布置舰队...";
    t[static_cast<std::size_t>(StrId::NetYourTurnFire)] = "轮到你了，开火！";
    t[static_cast<std::size_t>(StrId::NetShotFiredWaiting)] = "已开火，等待结果...";
    t[static_cast<std::size_t>(StrId::NetWaitingOpponentAttack)] = "等待对方攻击...";
    t[static_cast<std::size_t>(StrId::NetVictory)] = "胜利！";
    t[static_cast<std::size_t>(StrId::NetDefeat)] = "失败...";
    t[static_cast<std::size_t>(StrId::NetEnemySunkPrefix)] = "  已击沉: ";
    t[static_cast<std::size_t>(StrId::ConfirmAbandonTitle)] = "要中断对战吗？";
    t[static_cast<std::size_t>(StrId::ConfirmAbandonNetHint)] = "将断开与对手的连接。";
    t[static_cast<std::size_t>(StrId::ConfirmYes)] = "是";
    t[static_cast<std::size_t>(StrId::ConfirmNo)] = "否";

    t[static_cast<std::size_t>(StrId::BattleStatsLine)] = "%s %d/%d (%.0f%%)";
    t[static_cast<std::size_t>(StrId::BattleStatsSunkLine)] = "击沉%d / 损失%d";

    t[static_cast<std::size_t>(StrId::KeyGuidePlacementMove)] = "%s:移动  Enter:放置";
    t[static_cast<std::size_t>(StrId::KeyGuidePlacementModify)] = "R:旋转  X:随机布置";
    t[static_cast<std::size_t>(StrId::KeyGuidePlacementUndo)] = "BS:撤销  Esc:返回";
    t[static_cast<std::size_t>(StrId::KeyGuideBattleAim)] = "%s:瞄准  Enter:开火";
    t[static_cast<std::size_t>(StrId::KeyGuideBattleEsc)] = "Esc:中断";
    t[static_cast<std::size_t>(StrId::KeyGuideHandover)] = "Enter:开始  Esc:中断";
    return t;
}();
// clang-format on

}  // namespace i18n::detail
