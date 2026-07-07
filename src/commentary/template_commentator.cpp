/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "commentary/template_commentator.h"

#include <array>
#include <cstdio>

#include "commentary/snapshot_summary.h"
#include "core/types.h"

namespace battleship::commentary {

using battleship::core::MatchSnapshot;

namespace {

/// Phrase banks, one struct per language (EN from Phase 3b unchanged; JA/ZH
/// added in Phase 5). Each entry may contain "%s" once, filled in with an
/// event-specific detail (ship name for *Sunk events, localized via the
/// per-language ship_name_for below); entries without "%s" are used
/// verbatim. Kept as plain arrays (not a map-of-vectors) since CommentEvent
/// is a small closed enum switched on directly -- see comment_for().
struct PhraseBank {
    std::array<const char*, 3> match_start;
    std::array<const char*, 3> human_miss;
    std::array<const char*, 3> human_hit;
    std::array<const char*, 2> human_sunk;   ///< "%s" = localized enemy ship name
    std::array<const char*, 3> cpu_miss;
    std::array<const char*, 3> cpu_hit;
    std::array<const char*, 2> cpu_sunk;     ///< "%s" = localized own ship name
    std::array<const char*, 2> match_over;
};

constexpr PhraseBank kEnglishBank = {
    /*match_start=*/{{
        "New match! Let's see how this fleet engagement unfolds.",
        "Anchors aweigh -- the battle begins.",
        "Both fleets are in position. First shots incoming.",
    }},
    /*human_miss=*/{{
        "Splash -- that one missed.",
        "No luck that time, just open water.",
        "A near miss, nothing but sea out there.",
    }},
    /*human_hit=*/{{
        "Direct hit! The CPU's fleet takes damage.",
        "Contact! That shot found a hull.",
        "Nice shot -- something's leaking over there now.",
    }},
    /*human_sunk=*/{{
        "Down she goes! The CPU's %s is sunk.",
        "Sunk! That's the CPU's %s gone for good.",
    }},
    /*cpu_miss=*/{{
        "The CPU fires and misses -- lucky break.",
        "CPU's shot goes wide, your fleet is safe for now.",
        "Another splash from the CPU's side.",
    }},
    /*cpu_hit=*/{{
        "Ouch -- the CPU just scored a hit on your fleet.",
        "The CPU found one of your ships.",
        "Taking fire -- the CPU landed a hit.",
    }},
    /*cpu_sunk=*/{{
        "Your %s has been sunk by the CPU!",
        "Bad news -- your %s just went down.",
    }},
    /*match_over=*/{{
        "That's the match! A hard-fought battle either way.",
        "Game over -- what a battle that was.",
    }},
};

constexpr PhraseBank kJapaneseBank = {
    /*match_start=*/{{
        "新しい対戦の開始です。どんな海戦になるでしょうか。",
        "いざ出撃、戦いの幕開けです。",
        "両艦隊、配置完了。最初の一撃はどちらに？",
    }},
    /*human_miss=*/{{
        "残念、海面に水しぶきが上がっただけです。",
        "はずれ。そこには何もいないようです。",
        "惜しい！ 次の一撃に期待しましょう。",
    }},
    /*human_hit=*/{{
        "命中！ CPUの艦隊にダメージです。",
        "手応えあり！ 何かに当たりました。",
        "見事な射撃、敵艦を捉えました。",
    }},
    /*human_sunk=*/{{
        "撃沈！ CPUの%sを沈めました。",
        "やりました、CPUの%sが海に消えました。",
    }},
    /*cpu_miss=*/{{
        "CPUの攻撃ははずれ。今のところ無事です。",
        "CPUの砲撃は海に消えました。ラッキー！",
        "またも水柱だけ。CPUは狙いを外しました。",
    }},
    /*cpu_hit=*/{{
        "被弾！ CPUの攻撃が命中しました。",
        "CPUに艦を見つけられたようです。",
        "痛い一撃、こちらの艦がダメージを受けました。",
    }},
    /*cpu_sunk=*/{{
        "%sが撃沈されました！ 反撃のときです。",
        "残念、%sが沈められてしまいました。",
    }},
    /*match_over=*/{{
        "決着です！ 見応えのある海戦でした。",
        "試合終了。熱い戦いでした。",
    }},
};

constexpr PhraseBank kChineseBank = {
    /*match_start=*/{{
        "新的对战开始了，这场海战会如何展开？",
        "起锚出击，战斗打响了。",
        "双方舰队就位，第一炮即将打响。",
    }},
    /*human_miss=*/{{
        "可惜，只溅起了一片水花。",
        "未命中，那里似乎什么都没有。",
        "差一点！期待下一发吧。",
    }},
    /*human_hit=*/{{
        "命中！CPU的舰队受创了。",
        "打中了！这一炮找到了目标。",
        "漂亮的一击，敌舰中弹了。",
    }},
    /*human_sunk=*/{{
        "击沉！CPU的%s沉入海底了。",
        "干得漂亮，CPU的%s被消灭了。",
    }},
    /*cpu_miss=*/{{
        "CPU的炮弹落空了，暂时安全。",
        "CPU这一炮打偏了，真走运！",
        "又是一片水花，CPU没打中。",
    }},
    /*cpu_hit=*/{{
        "中弹了！CPU的攻击命中了我方。",
        "CPU似乎发现了你的舰船。",
        "挨了一炮，我方舰船受损。",
    }},
    /*cpu_sunk=*/{{
        "%s被击沉了！该反击了。",
        "糟糕，%s被CPU击沉了。",
    }},
    /*match_over=*/{{
        "胜负已分！这是一场精彩的海战。",
        "战斗结束，打得真激烈。",
    }},
};

const PhraseBank& bank_for(Lang lang) {
    switch (lang) {
        case Lang::EN:
            return kEnglishBank;
        case Lang::JA:
            return kJapaneseBank;
        case Lang::ZH:
            return kChineseBank;
    }
    return kEnglishBank;
}

/// Ship name in the given language. Deliberately duplicated (small, closed
/// set) rather than depending on i18n::ship_name(): battleship_commentary
/// must not link battleship_i18n (which itself depends on this library for
/// the Lang enum -- linking back would be cyclic), and the commentator's
/// language is its own lang_ member (set via set_language()), not the
/// global UI language, so ship names here must follow the commentator's
/// setting even if the two ever diverge.
const char* ship_name_for(Lang lang, battleship::core::ShipType ship) {
    using battleship::core::ShipType;
    switch (lang) {
        case Lang::JA:
            switch (ship) {
                case ShipType::Carrier:
                    return "空母";
                case ShipType::Battleship:
                    return "戦艦";
                case ShipType::Cruiser:
                    return "巡洋艦";
                case ShipType::Submarine:
                    return "潜水艦";
                case ShipType::Destroyer:
                    return "駆逐艦";
            }
            break;
        case Lang::ZH:
            switch (ship) {
                case ShipType::Carrier:
                    return "航母";
                case ShipType::Battleship:
                    return "战舰";
                case ShipType::Cruiser:
                    return "巡洋舰";
                case ShipType::Submarine:
                    return "潜艇";
                case ShipType::Destroyer:
                    return "驱逐舰";
            }
            break;
        case Lang::EN:
            break;
    }
    switch (ship) {
        case ShipType::Carrier:
            return "Carrier";
        case ShipType::Battleship:
            return "Battleship";
        case ShipType::Cruiser:
            return "Cruiser";
        case ShipType::Submarine:
            return "Submarine";
        case ShipType::Destroyer:
            return "Destroyer";
    }
    return "Destroyer";
}

template <std::size_t N>
std::string pick(const std::array<const char*, N>& phrases, std::mt19937_64& rng) {
    std::uniform_int_distribution<std::size_t> dist(0, N - 1);
    return phrases[dist(rng)];
}

std::string format_with_ship(const std::string& phrase, Lang lang,
                             battleship::core::ShipType ship) {
    // std::snprintf accepts %s substitution against a UTF-8 byte string; the
    // phrase templates above are compile-time-fixed literals (never user
    // input), so this is safe from format-string injection. 160 bytes covers
    // the longest CJK phrase (~40 chars x 3 bytes each) with headroom.
    char buf[160];
    std::snprintf(buf, sizeof(buf), phrase.c_str(), ship_name_for(lang, ship));
    return buf;
}

}  // namespace

TemplateCommentator::TemplateCommentator(std::uint64_t seed) : rng_(seed) {}

void TemplateCommentator::request_comment(const MatchSnapshot& snapshot, CommentEvent event,
                                          const CommentContext& context) {
    const std::string text = comment_for(snapshot, event, context);
    if (callback_) {
        callback_(text);
    }
}

std::string TemplateCommentator::comment_for(const MatchSnapshot& /*snapshot*/, CommentEvent event,
                                             const CommentContext& context) {
    const PhraseBank& bank = bank_for(lang_);
    switch (event) {
        case CommentEvent::MatchStart:
            return pick(bank.match_start, rng_);
        case CommentEvent::HumanMiss:
            return pick(bank.human_miss, rng_);
        case CommentEvent::HumanHit:
            return pick(bank.human_hit, rng_);
        case CommentEvent::HumanSunk:
            return format_with_ship(pick(bank.human_sunk, rng_), lang_, context.sunk_ship);
        case CommentEvent::CpuMiss:
            return pick(bank.cpu_miss, rng_);
        case CommentEvent::CpuHit:
            return pick(bank.cpu_hit, rng_);
        case CommentEvent::CpuSunk:
            return format_with_ship(pick(bank.cpu_sunk, rng_), lang_, context.sunk_ship);
        case CommentEvent::MatchOver:
            return pick(bank.match_over, rng_);
    }
    return "";
}

void TemplateCommentator::cancel_pending() {
    // Nothing in flight -- request_comment() is synchronous.
}

void TemplateCommentator::set_callback(CommentCallback callback) {
    callback_ = std::move(callback);
}

void TemplateCommentator::set_language(Lang lang) {
    lang_ = lang;
}

Lang TemplateCommentator::language() const {
    return lang_;
}

}  // namespace battleship::commentary
