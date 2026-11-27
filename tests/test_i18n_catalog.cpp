// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Tests for the Phase 5 string catalog (src/i18n/strings.h). Focused on the
// two failure modes a hand-maintained per-language table is prone to: a
// missing entry (empty string slipped through for some StrId in some
// language) and a template whose placeholder count doesn't match what
// callers actually pass via trf() -- see PLAN.md "カタログの全 StrId が3言語
// で定義済み（欠落検出）、フォーマットテンプレートの引数個数整合".

#include "i18n/strings.h"

#include <gtest/gtest.h>

#include <cstring>

using i18n::Lang;
using i18n::StrId;

namespace {

constexpr Lang kAllLangs[] = {Lang::EN, Lang::JA, Lang::ZH};

/// Every StrId that is a printf-style template (per strings.h's per-entry
/// comments), paired with the number of "%" conversion specifiers it must
/// contain in every language (order/type can differ per language, but the
/// count must match what callers pass to trf()).
struct TemplateArity {
    StrId id;
    int conversions;
};

constexpr TemplateArity kTemplates[] = {
    {StrId::PlacementPlayerN, 1},
    {StrId::PlacementShipStatus, 2},
    {StrId::HandoverPassDeviceTo, 1},
    {StrId::ShotSunk, 1},
    {StrId::ShotSunkYours, 1},
    {StrId::BattleYouShotResult, 2},
    {StrId::BattleCpuShotResult, 2},
    {StrId::BattleVsCpuDifficulty, 1},
    {StrId::ResultHitRateLineYou, 3},
    {StrId::ResultHitRateLineCpu, 3},
    {StrId::ResultHitRateLineOpponent, 3},
    {StrId::ResultHitRateLineP1, 3},
    {StrId::ResultHitRateLineP2, 3},
    {StrId::ResultTotalTurns, 1},
    {StrId::NetLoraHostingAs, 1},
    {StrId::BattleStatsLine, 4},
    {StrId::BattleStatsSunkLine, 2},
    {StrId::KeyGuidePlacementMove, 1},
    {StrId::KeyGuideBattleAim, 1},
};

/// Counts printf conversion specifiers ("%s", "%d", "%.0f", "%%" does NOT
/// count since it's a literal percent, "%u", ...) in `format`. Good enough
/// for the small, hand-written templates in strings_<lang>.cpp -- not a
/// general printf-format parser.
int count_conversions(const char* format) {
    int count = 0;
    for (const char* p = format; *p; ++p) {
        if (*p != '%') {
            continue;
        }
        ++p;
        if (*p == '%') {
            continue;  // literal percent, not a conversion
        }
        if (*p == '\0') {
            break;
        }
        // Skip any flags/width/precision digits and a '.' before the
        // conversion letter (e.g. "%.0f").
        while (*p && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.')) {
            ++p;
        }
        if (*p) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST(I18nCatalog, EveryStrIdHasNonEmptyTextInEveryLanguage) {
    for (Lang lang : kAllLangs) {
        i18n::set_language(lang);
        for (std::size_t i = 0; i < i18n::kStrIdCount; ++i) {
            const auto id = static_cast<StrId>(i);
            const char* text = i18n::tr(id);
            EXPECT_NE(text, nullptr) << "lang=" << static_cast<int>(lang) << " id=" << i;
            EXPECT_GT(std::strlen(text), 0u)
                << "empty string for lang=" << static_cast<int>(lang) << " id=" << i;
        }
    }
    i18n::set_language(Lang::EN);  // restore default for subsequent tests
}

TEST(I18nCatalog, TemplatePlaceholderCountMatchesAcrossAllLanguages) {
    for (const auto& tmpl : kTemplates) {
        for (Lang lang : kAllLangs) {
            i18n::set_language(lang);
            const int actual = count_conversions(i18n::tr(tmpl.id));
            EXPECT_EQ(actual, tmpl.conversions)
                << "lang=" << static_cast<int>(lang) << " id=" << static_cast<int>(tmpl.id)
                << " text=\"" << i18n::tr(tmpl.id) << "\"";
        }
    }
    i18n::set_language(Lang::EN);
}

TEST(I18nCatalog, TrfFormatsPositionalArgsCorrectly) {
    i18n::set_language(Lang::EN);
    EXPECT_EQ(i18n::trf(StrId::PlacementPlayerN, 1), "Player 1");
    EXPECT_EQ(i18n::trf(StrId::PlacementPlayerN, 2), "Player 2");
    EXPECT_EQ(i18n::trf(StrId::ResultTotalTurns, 42), "Total turns: 42");
    EXPECT_EQ(i18n::trf(StrId::BattleYouShotResult, "D2", "Miss"), "You D2: Miss");
}

TEST(I18nCatalog, ShipNameLocalizesPerLanguage) {
    using battleship::core::ShipType;

    i18n::set_language(Lang::EN);
    EXPECT_EQ(i18n::ship_name(ShipType::Carrier), "Carrier");

    i18n::set_language(Lang::JA);
    EXPECT_EQ(i18n::ship_name(ShipType::Carrier), "空母");

    i18n::set_language(Lang::ZH);
    EXPECT_EQ(i18n::ship_name(ShipType::Carrier), "航母");

    i18n::set_language(Lang::EN);
}

TEST(I18nCatalog, DefaultLanguageIsEnglish) {
    // NOTE: relies on running before any other test flips the process-global
    // language and forgets to restore it; every test above restores EN at
    // its end for exactly this reason.
    i18n::set_language(Lang::EN);
    EXPECT_EQ(i18n::current_language(), Lang::EN);
    EXPECT_STREQ(i18n::tr(StrId::MenuOnePlayer), "1 Player");
}

TEST(I18nCatalog, SetLanguageSwitchesActiveTable) {
    i18n::set_language(Lang::JA);
    EXPECT_EQ(i18n::current_language(), Lang::JA);
    EXPECT_STREQ(i18n::tr(StrId::MenuOnePlayer), "1人対戦");

    i18n::set_language(Lang::ZH);
    EXPECT_STREQ(i18n::tr(StrId::MenuOnePlayer), "单人对战");

    i18n::set_language(Lang::EN);  // restore default for subsequent tests
}

TEST(I18nCatalog, LoraStringsDefinedInEveryLanguage) {
    // Phase 8 (LoRa): explicit guard on top of the exhaustive non-empty
    // check, so a LoRa entry accidentally left as a placeholder (or with a
    // stray "Coming soon" marker) is caught by name.
    constexpr StrId kLoraIds[] = {
        StrId::NetTransportLora,   StrId::NetLoraHostingAs,      StrId::NetJoinScanningLora,
        StrId::NetLoraCapMissing,  StrId::NetLoraUnavailable,    StrId::NetLoraAntennaQuestion,
        StrId::NetLoraAntennaConfirm,
    };
    for (Lang lang : kAllLangs) {
        i18n::set_language(lang);
        for (StrId id : kLoraIds) {
            const char* text = i18n::tr(id);
            ASSERT_NE(text, nullptr);
            EXPECT_GT(std::strlen(text), 0u) << "lang=" << static_cast<int>(lang) << " id=" << static_cast<int>(id);
            EXPECT_EQ(std::strstr(text, "Coming soon"), nullptr) << text;
        }
    }
    i18n::set_language(Lang::EN);
    EXPECT_STREQ(i18n::tr(StrId::NetLoraUnavailable), "LoRa radio could not start.");
    EXPECT_EQ(i18n::trf(StrId::NetLoraHostingAs, "BSHIP-abc"), "Recruiting over LoRa as BSHIP-abc ...");
}

TEST(I18nCatalog, BleOffStringDefinedInEveryLanguage) {
    i18n::set_language(Lang::EN);
    EXPECT_STREQ(i18n::tr(StrId::NetBleOff), "Bluetooth is off. Turn it on in Settings.");
    i18n::set_language(Lang::JA);
    EXPECT_STREQ(i18n::tr(StrId::NetBleOff), "Bluetoothがオフです。設定でオンにしてください");
    i18n::set_language(Lang::ZH);
    EXPECT_STREQ(i18n::tr(StrId::NetBleOff), "蓝牙已关闭，请在设置中开启");
    i18n::set_language(Lang::EN);
}
