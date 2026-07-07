// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Tests for commentary::TemplateCommentator: the always-available fallback
// commentator. Verifies phrase selection is non-empty, varies across seeds
// (not stuck on a single canned line), stays deterministic for a fixed seed,
// includes the sunk ship name for *Sunk events, and delivers synchronously
// via the callback (no threads involved in this implementation).

#include "commentary/template_commentator.h"

#include <gtest/gtest.h>

#include <set>

#include "commentary/snapshot_summary.h"
#include "core/snapshot.h"
#include "core/types.h"

using namespace battleship::commentary;
using battleship::core::MatchSnapshot;
using battleship::core::ShipType;

namespace {

MatchSnapshot make_snapshot() {
    return MatchSnapshot{};
}

}  // namespace

TEST(TemplateCommentator, ProducesNonEmptyTextForEveryEvent) {
    TemplateCommentator commentator(42);
    const MatchSnapshot snapshot = make_snapshot();

    const CommentEvent events[] = {
        CommentEvent::MatchStart, CommentEvent::HumanMiss, CommentEvent::HumanHit,
        CommentEvent::HumanSunk,  CommentEvent::CpuMiss,   CommentEvent::CpuHit,
        CommentEvent::CpuSunk,    CommentEvent::MatchOver,
    };
    for (auto event : events) {
        const std::string text = commentator.comment_for(snapshot, event);
        EXPECT_FALSE(text.empty()) << "event=" << event_key(event);
    }
}

TEST(TemplateCommentator, SameSeedProducesIdenticalSequence) {
    TemplateCommentator a(123);
    TemplateCommentator b(123);
    const MatchSnapshot snapshot = make_snapshot();

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(a.comment_for(snapshot, CommentEvent::HumanHit),
                  b.comment_for(snapshot, CommentEvent::HumanHit));
    }
}

TEST(TemplateCommentator, PhraseVariesAcrossManyDrawsForSameEvent) {
    TemplateCommentator commentator(7);
    const MatchSnapshot snapshot = make_snapshot();

    std::set<std::string> seen;
    for (int i = 0; i < 50; ++i) {
        seen.insert(commentator.comment_for(snapshot, CommentEvent::HumanMiss));
    }
    // The phrase bank has more than one entry; over 50 draws we should see
    // at least 2 distinct phrases (extremely unlikely to fail by chance if
    // the RNG tie-break is wired correctly).
    EXPECT_GT(seen.size(), 1u);
}

TEST(TemplateCommentator, HumanSunkEventMentionsShipName) {
    TemplateCommentator commentator(5);
    const MatchSnapshot snapshot = make_snapshot();
    CommentContext context;
    context.sunk_ship = ShipType::Carrier;

    const std::string text = commentator.comment_for(snapshot, CommentEvent::HumanSunk, context);
    EXPECT_NE(text.find("Carrier"), std::string::npos);
}

TEST(TemplateCommentator, CpuSunkEventMentionsShipName) {
    TemplateCommentator commentator(9);
    const MatchSnapshot snapshot = make_snapshot();
    CommentContext context;
    context.sunk_ship = ShipType::Submarine;

    const std::string text = commentator.comment_for(snapshot, CommentEvent::CpuSunk, context);
    EXPECT_NE(text.find("Submarine"), std::string::npos);
}

TEST(TemplateCommentator, RequestCommentDeliversSynchronouslyViaCallback) {
    TemplateCommentator commentator(1);
    const MatchSnapshot snapshot = make_snapshot();

    std::string received;
    bool called = false;
    commentator.set_callback([&](const std::string& text) {
        received = text;
        called = true;
    });

    commentator.request_comment(snapshot, CommentEvent::MatchStart);
    EXPECT_TRUE(called);
    EXPECT_FALSE(received.empty());
}

TEST(TemplateCommentator, CancelPendingIsSafeNoOp) {
    TemplateCommentator commentator(1);
    // Should not crash even with no request in flight, and should not
    // prevent subsequent requests from delivering.
    commentator.cancel_pending();

    bool called = false;
    commentator.set_callback([&](const std::string&) { called = true; });
    commentator.request_comment(make_snapshot(), CommentEvent::MatchOver);
    EXPECT_TRUE(called);
}

TEST(TemplateCommentator, LanguageDefaultsToEnglishAndIsSettable) {
    TemplateCommentator commentator(1);
    EXPECT_EQ(commentator.language(), Lang::EN);

    commentator.set_language(Lang::JA);
    EXPECT_EQ(commentator.language(), Lang::JA);
}

TEST(TemplateCommentator, JapaneseAndChineseBanksProduceNonEmptyTextForEveryEvent) {
    const CommentEvent events[] = {
        CommentEvent::MatchStart, CommentEvent::HumanMiss, CommentEvent::HumanHit,
        CommentEvent::HumanSunk,  CommentEvent::CpuMiss,   CommentEvent::CpuHit,
        CommentEvent::CpuSunk,    CommentEvent::MatchOver,
    };
    for (Lang lang : {Lang::JA, Lang::ZH}) {
        TemplateCommentator commentator(42);
        commentator.set_language(lang);
        for (auto event : events) {
            const std::string text = commentator.comment_for(make_snapshot(), event);
            EXPECT_FALSE(text.empty())
                << "lang=" << static_cast<int>(lang) << " event=" << event_key(event);
            // A JA/ZH phrase must actually contain non-ASCII (CJK) bytes --
            // catching a copy-paste of the English bank into the wrong slot.
            bool has_non_ascii = false;
            for (unsigned char c : text) {
                if (c > 0x7f) {
                    has_non_ascii = true;
                    break;
                }
            }
            EXPECT_TRUE(has_non_ascii)
                << "lang=" << static_cast<int>(lang) << " event=" << event_key(event)
                << " text=" << text;
        }
    }
}

TEST(TemplateCommentator, SunkEventsUseLocalizedShipNames) {
    const MatchSnapshot snapshot = make_snapshot();
    CommentContext context;
    context.sunk_ship = ShipType::Carrier;

    TemplateCommentator ja(5);
    ja.set_language(Lang::JA);
    const std::string ja_text = ja.comment_for(snapshot, CommentEvent::HumanSunk, context);
    EXPECT_NE(ja_text.find("空母"), std::string::npos) << ja_text;
    EXPECT_EQ(ja_text.find("Carrier"), std::string::npos) << ja_text;

    TemplateCommentator zh(5);
    zh.set_language(Lang::ZH);
    const std::string zh_text = zh.comment_for(snapshot, CommentEvent::HumanSunk, context);
    EXPECT_NE(zh_text.find("航母"), std::string::npos) << zh_text;
    EXPECT_EQ(zh_text.find("Carrier"), std::string::npos) << zh_text;
}
