/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "commentary/commentator_factory.h"
#include "i18n/settings.h"
#include "net/ble/ble_transport.h"  // kBleNamePrefix only (header is safe on every platform)
#include "net/lora/japan920_policy.h"

using battleship::core::Coord;
using battleship::core::MatchStatus;
using battleship::core::Orientation;
using battleship::core::PlayerId;
using battleship::core::Ship;
using battleship::core::ShipType;
using battleship::core::ShotOutcome;
using battleship::core::kAllShipTypes;
using battleship::core::kShipTypeCount;
using battleship::core::other_player;
using battleship::core::place_fleet_randomly;

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

/// Delay before the AI fires back, so the human's own shot result stays
/// readable on the status line for a beat before the CPU's reply lands.
constexpr uint32_t kAiTurnDelayMs = 500;

/// How often commentary_poll_timer_ checks for a completed async comment.
/// 200ms is frequent enough that a comment appears promptly after landing
/// (LLM generations run 400-900ms per the Phase 3b latency measurements)
/// without burning CPU on a tight poll loop.
constexpr uint32_t kCommentaryPollIntervalMs = 200;

/// "B4"-style coordinate label (column A-J, row 1-10) for status-line text.
std::string coord_label(Coord c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%d", static_cast<char>('A' + c.x), c.y + 1);
    return buf;
}

// ---- Network play (Phase 4) ----

/// Default TCP port a host listens on. Distinct from the UDP discovery port
/// (net::kDiscoveryPort, 35353) so both sockets can coexist. If this port is
/// taken (e.g. a second instance on the same machine during 2-process
/// desktop testing), the host falls back to an OS-assigned port, which the
/// discovery announcement then advertises.
constexpr uint16_t kNetGamePort = 35354;

/// How often the network poll timer services session/announcer/discoverer.
/// 100ms keeps worst-case added latency well under one LVGL redraw's notice
/// while staying far coarser than needed for the protocol timeouts
/// (ACK 400ms, PING 2s, disconnect 10s) -- and never blocks the LVGL thread
/// (every net/ API is non-blocking poll style).
constexpr uint32_t kNetPollIntervalMs = 100;

// ---- LoRa (Phase 8) ----

/// How long closed LoRa objects are kept alive after teardown so a pending
/// BYE can still be transmitted (LoRaLink retries it for
/// LinkTiming::bye_window_ms = 500 ms; 100 ms of margin on top).
constexpr uint64_t kLoraCloseDrainMs = 600;
/// Poll period of the closing holder's timer while the BYE drains.
constexpr uint32_t kLoraClosePollMs = 20;

/// Destroys a radio handle in the ownership order: radio first, then the
/// Cap power (whose destructor switches the 5V output off). Plain
/// move-assignment of a RadioHandle would assign -- and so destroy -- the
/// power member first, hence this helper.
void release_radio(battleship::net::lora::RadioHandle& handle) {
    if (!handle.radio && !handle.power) {
        return;
    }
    handle.radio.reset();
    handle.power.reset();
    // Lifecycle trace for on-device power checks (ext_5v_out) and the
    // desktop 2-process runs; stdout is line-buffered (see app.cpp).
    std::printf("[lora] radio released\n");
}

/// open_radio()/link failure -> user-facing message (controller ruling):
/// a missing Cap shows up as ExpanderMissing / RadioNoResponse; anything
/// else is reported as a generic "radio could not start".
i18n::StrId lora_error_message(battleship::net::lora::RadioInitError error) {
    using battleship::net::lora::RadioInitError;
    switch (error) {
        case RadioInitError::ExpanderMissing:
        case RadioInitError::RadioNoResponse:
            return i18n::StrId::NetLoraCapMissing;
        default:
            return i18n::StrId::NetLoraUnavailable;
    }
}

/// "You B4: Hit!" / "Foe C7: Sunk your Cruiser!"-style status fragment.
/// `who_id` is i18n::StrId::NetYou or NetFoe; the coordinate stays in the
/// untranslated A1-J10 notation in every language (PLAN.md Phase 5:
/// "座標=そのまま").
std::string net_shot_text(i18n::StrId who_id, Coord target,
                          const battleship::core::ShotResult& result, bool own_shot) {
    std::string text = std::string(i18n::tr(who_id)) + " " + coord_label(target) + ": ";
    switch (result.outcome) {
        case ShotOutcome::Miss:
            text += i18n::tr(i18n::StrId::ShotMiss);
            break;
        case ShotOutcome::Hit:
            text += i18n::tr(i18n::StrId::ShotHit);
            break;
        case ShotOutcome::Sunk:
            text += i18n::trf(own_shot ? i18n::StrId::ShotSunk : i18n::StrId::ShotSunkYours,
                              i18n::ship_name(result.sunk_ship).c_str());
            break;
        default:
            break;
    }
    return text;
}

} // namespace

/// Closed LoRa objects kept alive briefly so the BYE can drain. Member order
/// gives destruction transport -> link -> radio (radio, then Cap power).
struct BaseViewModel::LoraClosing {
    battleship::net::lora::RadioHandle radio;
    std::unique_ptr<battleship::net::lora::LoRaLink> link;
    std::unique_ptr<battleship::net::lora::LoRaTransport> transport;
    uint64_t deadline_ms = 0;
};

BaseViewModel::BaseViewModel()
    : commentator_(battleship::commentary::make_default_commentator()),
      title_subject_(model_.app_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      board_dirty_subject_(0),
      status_text_subject_(""),
      commentary_text_subject_(""),
      net_status_subject_(""),
      net_hosts_dirty_subject_(0),
      language_version_subject_(0) {
    if (commentator_) {
        // i18n::load_settings() (app.cpp, before this constructor runs) has
        // already resolved i18n::current_language() from the persisted
        // setting or left it at the English default -- propagate it to the
        // commentator so the very first MatchStart comment (Phase 3b) is
        // already in the right language, not just labels built afterward.
        commentator_->set_language(i18n::current_language());
    }
    if (const char* autopilot = std::getenv("BATTLESHIP_NET_AUTOPILOT")) {
        // Test tooling for scripted 2-process desktop runs (see NOTES.md
        // Phase 4): plays the network match automatically. Key-script-driven
        // battles proved too timing-sensitive (a shot only fires on the
        // firing player's turn), so the autopilot fires exactly when the
        // turn arrives instead of blindly mashing Enter on a timer.
        if (std::string(autopilot) == "win") {
            net_autopilot_mode_ = 1;
        } else if (std::string(autopilot) == "lose") {
            net_autopilot_mode_ = 2;
        }
    }

    if (commentator_) {
        // The callback may run on a worker thread (LlmCommentator) or
        // synchronously on the calling thread (TemplateCommentator) --
        // either way, it must not touch LVGL directly, so it only stashes
        // the text; commentary_poll_timer_cb() (LVGL thread) publishes it.
        commentator_->set_callback([this](const std::string& text) {
            std::lock_guard<std::mutex> lock(commentary_mutex_);
            pending_commentary_ = text;
            commentary_pending_ = true;
        });
        commentary_poll_timer_ =
            lv_timer_create(&BaseViewModel::commentary_poll_timer_cb, kCommentaryPollIntervalMs, this);
    }
}

BaseViewModel::~BaseViewModel() {
    teardown_network();
    // Application exit: no waiting for the BYE window (close() already made
    // the immediate BYE attempt) -- destroy radio and release power now.
    net_lora_finish_closing();
    cancel_ai_turn();
    if (commentary_poll_timer_) {
        lv_timer_delete(commentary_poll_timer_);
        commentary_poll_timer_ = nullptr;
    }
    if (commentator_) {
        commentator_->cancel_pending();
    }
}

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::board_dirty_subject() {
    return board_dirty_subject_.native();
}

lv_subject_t* BaseViewModel::status_text_subject() {
    return status_text_subject_.native();
}

lv_subject_t* BaseViewModel::commentary_text_subject() {
    return commentary_text_subject_.native();
}

lv_subject_t* BaseViewModel::language_version_subject() {
    return language_version_subject_.native();
}

battleship::commentary::Lang BaseViewModel::language() const {
    return i18n::current_language();
}

void BaseViewModel::set_language(battleship::commentary::Lang lang) {
    i18n::save_language(lang);  // applies immediately (i18n::set_language) + persists to disk
    if (commentator_) {
        commentator_->set_language(lang);
    }
    // Bump first so ScreenManager's reload (triggered by this subject) sees
    // the already-applied language when it rebuilds the current screen and
    // every label re-reads tr() (PLAN.md: "選択即反映").
    language_version_subject_.set(language_version_subject_.value() + 1);
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_page(model::AppPage page) {
    if (page == model::AppPage::Menu &&
        (net_session_ || net_transport_ || net_discovering_ || net_announcing_ || net_poll_timer_ ||
         net_lora_link_)) {
        // Returning to the title from anywhere in the network flow (Result's
        // Title button, the NetError dialog, ...) always abandons the
        // session/lobby/discovery -- there is no way to "background" a
        // network match.
        teardown_network();
    }
    model_.set_current_page(page);
    publish_all();
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::request_safe_quit() {
    // Long Esc / Home (App Submission Guide "safe exit from anywhere"):
    // best-effort notify a connected peer before tearing the network stack
    // down, exactly like confirm_interrupt(), but reachable from any page
    // (menus included) and without touching AppPage navigation -- the
    // process is about to exit, so there is no "back" screen to show.
    if (net_session_) {
        net_session_->abandon(lv_tick_get());
    }
    teardown_network();
    request_quit();
}

lv_subject_t* BaseViewModel::interrupt_modal_subject() {
    return interrupt_modal_subject_.native();
}

lv_subject_t* BaseViewModel::title_quit_armed_subject() {
    return title_quit_armed_subject_.native();
}

void BaseViewModel::set_title_quit_armed(bool armed) {
    if (title_quit_armed_subject_.value() != armed) {
        title_quit_armed_subject_.set(armed);
    }
}

bool BaseViewModel::title_quit_armed() const {
    return title_quit_armed_subject_.value();
}

bool BaseViewModel::is_match_interruptible() const {
    const auto page = current_page();
    const bool in_match_page = page == model::AppPage::Placement ||
                               page == model::AppPage::Handover ||
                               page == model::AppPage::Battle;
    if (!in_match_page) {
        return false;
    }
    if (match_mode_ == MatchMode::Network) {
        // Any live network flow on a match page needs a confirmation: even
        // during Placement the peer is already connected and backing out
        // means disconnecting them.
        if (!net_session_) {
            return false;
        }
        const auto phase = net_session_->phase();
        return phase != battleship::net::SessionPhase::Finished &&
               phase != battleship::net::SessionPhase::Disconnected;
    }
    // Local (hotseat / VsAi) matches: only an actually-in-progress Battle is
    // worth a prompt; Esc during local Placement/Handover just backs out to
    // the title directly (no second human loses anything).
    return page == model::AppPage::Battle &&
           game_.status() == battleship::core::MatchStatus::InProgress;
}

void BaseViewModel::request_interrupt_confirm() {
    if (interrupt_modal_visible_) {
        return;
    }
    interrupt_modal_visible_ = true;
    interrupt_modal_subject_.set(true);
}

void BaseViewModel::dismiss_interrupt_confirm() {
    if (!interrupt_modal_visible_) {
        return;
    }
    interrupt_modal_visible_ = false;
    interrupt_modal_subject_.set(false);
}

void BaseViewModel::confirm_interrupt() {
    dismiss_interrupt_confirm();
    cancel_ai_turn();
    if (net_session_) {
        // Best-effort explicit GAME_OVER so the peer learns of the abandon
        // immediately instead of waiting out the 10s PeerSilent timeout
        // (PLAN.md Phase 7: "相手に GAME_OVER/切断が正しく伝わること").
        net_session_->abandon(lv_tick_get());
    }
    // show_page(Menu) tears down the whole network stack as a side effect
    // (session/transport/announcer/discoverer -- see show_page()).
    show_page(model::AppPage::Menu);
}

void BaseViewModel::start_new_match() {
    cancel_ai_turn();
    game_ = battleship::core::Game{};
    placement_player_ = PlayerId::One;
    reset_placement_cursor();
    battle_cursor_ = Coord{0, 0};
    last_shot_ = LastShotInfo{};
    last_human_result_text_.clear();

    // Discard any in-flight/queued commentary from the previous match before
    // it can land after the fact (NOTES.md §8.5: "start_new_match() で
    // in-flight を破棄"), and clear whatever was left on screen.
    if (commentator_) {
        commentator_->cancel_pending();
    }
    {
        std::lock_guard<std::mutex> lock(commentary_mutex_);
        pending_commentary_.clear();
        commentary_pending_ = false;
    }
    commentary_text_subject_.set("");

    if (match_mode_ == MatchMode::VsAi) {
        // Fresh AiPlayer per match (rematch included) with a fresh seed so
        // the CPU doesn't replay the exact same shot sequence every rematch.
        ai_ = std::make_unique<battleship::core::ai::AiPlayer>(ai_difficulty_, rng_());
        // Seed the (battle-screen) status line with the chosen difficulty;
        // it is replaced by shot feedback once firing starts. The battle
        // title itself stays a short "You vs CPU" for layout reasons.
        i18n::StrId difficulty_id = i18n::StrId::DifficultyNormalLabel;
        switch (ai_difficulty_) {
            case battleship::core::ai::Difficulty::Easy:
                difficulty_id = i18n::StrId::DifficultyEasyLabel;
                break;
            case battleship::core::ai::Difficulty::Normal:
                difficulty_id = i18n::StrId::DifficultyNormalLabel;
                break;
            case battleship::core::ai::Difficulty::Hard:
                difficulty_id = i18n::StrId::DifficultyHardLabel;
                break;
        }
        status_text_subject_.set(
            i18n::trf(i18n::StrId::BattleVsCpuDifficulty, i18n::tr(difficulty_id)).c_str());
        request_commentary(battleship::commentary::CommentEvent::MatchStart);
    } else {
        ai_.reset();
        status_text_subject_.set("");
    }
    show_page(model::AppPage::Placement);
    bump_board_dirty();
}

void BaseViewModel::start_hotseat_match() {
    teardown_network();
    match_mode_ = MatchMode::Hotseat;
    start_new_match();
}

void BaseViewModel::start_ai_match(battleship::core::ai::Difficulty difficulty) {
    teardown_network();
    match_mode_ = MatchMode::VsAi;
    ai_difficulty_ = difficulty;
    start_new_match();
}

PlayerId BaseViewModel::viewing_player() const {
    if (match_mode_ == MatchMode::VsAi || match_mode_ == MatchMode::Network) {
        // The local player is always "One" from this device's perspective.
        return PlayerId::One;
    }
    return game_.current_turn();
}

void BaseViewModel::advance_from_placement() {
    if (match_mode_ == MatchMode::Network) {
        // Confirm placement -> send READY, then move straight to the Battle
        // screen, which renders the "Waiting for opponent..." state until
        // the peer's READY arrives (net_sync_session_state flips the status
        // line when SessionPhase reaches Battle). send_ready() returning
        // false means we're not in the Placement phase (e.g. a double
        // Confirm press) -- nothing to do then.
        if (net_session_ && net_session_->send_ready()) {
            battle_cursor_ = Coord{0, 0};
            status_text_subject_.set("");
            commentary_text_subject_.set(i18n::tr(i18n::StrId::NetWaitingOpponentPlacing));
            show_page(model::AppPage::Battle);
            bump_board_dirty();
        }
        return;
    }

    if (match_mode_ == MatchMode::VsAi) {
        // Human (One) just confirmed placement. Auto-place the AI's fleet
        // and go straight to Battle -- no second Placement screen and no
        // Handover in a single-player match.
        place_fleet_randomly(game_.board(PlayerId::Two), rng_);
        game_.start(PlayerId::One);
        show_page(model::AppPage::Battle);
        bump_board_dirty();
        return;
    }

    if (placement_player_ == PlayerId::One) {
        placement_player_ = PlayerId::Two;
        reset_placement_cursor();
        show_page(model::AppPage::Handover);
        bump_board_dirty();
        return;
    }

    // Both boards placed; kick off the match with player One going first,
    // then let the Handover screen "reveal" player One's turn.
    game_.start(PlayerId::One);
    show_page(model::AppPage::Handover);
    bump_board_dirty();
}

bool BaseViewModel::has_ship_to_place() const {
    return placement_board().ships().size() < static_cast<size_t>(kShipTypeCount);
}

ShipType BaseViewModel::next_ship_to_place() const {
    const auto& board = placement_board();
    for (auto type : kAllShipTypes) {
        bool placed = false;
        for (const auto& ship : board.ships()) {
            if (ship.type() == type) {
                placed = true;
                break;
            }
        }
        if (!placed) {
            return type;
        }
    }
    return ShipType::Destroyer;
}

void BaseViewModel::move_placement_cursor(int dx, int dy) {
    int x = placement_cursor_.x + dx;
    int y = placement_cursor_.y + dy;
    x = x < 0 ? 0 : (x >= battleship::core::kBoardSize ? battleship::core::kBoardSize - 1 : x);
    y = y < 0 ? 0 : (y >= battleship::core::kBoardSize ? battleship::core::kBoardSize - 1 : y);
    placement_cursor_ = Coord{x, y};
    bump_board_dirty();
}

void BaseViewModel::toggle_placement_orientation() {
    placement_orientation_ = placement_orientation_ == Orientation::Horizontal ? Orientation::Vertical
                                                                                : Orientation::Horizontal;
    bump_board_dirty();
}

bool BaseViewModel::is_placement_preview_valid() const {
    if (!has_ship_to_place()) {
        return false;
    }

    const Ship candidate(next_ship_to_place(), placement_cursor_, placement_orientation_);
    if (!candidate.fits_in_bounds()) {
        return false;
    }

    const auto& board = placement_board();
    for (const auto& existing : board.ships()) {
        if (candidate.overlaps(existing)) {
            return false;
        }
    }
    return true;
}

bool BaseViewModel::commit_placement() {
    if (!is_placement_preview_valid()) {
        return false;
    }

    const Ship candidate(next_ship_to_place(), placement_cursor_, placement_orientation_);
    const bool placed = active_placement_board().place_ship(candidate);
    if (placed) {
        bump_board_dirty();
    }
    return placed;
}

bool BaseViewModel::undo_last_placement() {
    auto& board = active_placement_board();
    if (board.ships().empty()) {
        return false;
    }

    // Board has no explicit "remove last ship" API; rebuild from all-but-last.
    std::vector<Ship> remaining(board.ships().begin(), board.ships().end() - 1);
    board.clear();
    for (const auto& ship : remaining) {
        board.place_ship(ship);
    }
    bump_board_dirty();
    return true;
}

void BaseViewModel::randomize_placement() {
    auto& board = active_placement_board();
    place_fleet_randomly(board, rng_);
    bump_board_dirty();
}

PlayerId BaseViewModel::upcoming_player() const {
    if (game_.status() == MatchStatus::Placement) {
        // Between the two placement screens: player Two is about to place.
        return PlayerId::Two;
    }
    return game_.current_turn();
}

void BaseViewModel::confirm_handover() {
    if (game_.status() == MatchStatus::Placement) {
        show_page(model::AppPage::Placement);
        return;
    }

    show_page(model::AppPage::Battle);
}

void BaseViewModel::move_battle_cursor(int dx, int dy) {
    int x = battle_cursor_.x + dx;
    int y = battle_cursor_.y + dy;
    x = x < 0 ? 0 : (x >= battleship::core::kBoardSize ? battleship::core::kBoardSize - 1 : x);
    y = y < 0 ? 0 : (y >= battleship::core::kBoardSize ? battleship::core::kBoardSize - 1 : y);
    battle_cursor_ = Coord{x, y};
    bump_board_dirty();
}

bool BaseViewModel::fire_at_cursor() {
    if (match_mode_ == MatchMode::Network) {
        if (!net_session_ || net_session_->phase() != battleship::net::SessionPhase::Battle) {
            return false;
        }
        if (!net_session_->can_fire()) {
            // Opponent's turn, or our previous shot's RESULT is still in
            // flight -- ignore the keypress (same UX as the VsAi guard).
            return false;
        }
        if (net_session_->player_view().tracking_board().was_shot_at(battle_cursor_)) {
            status_text_subject_.set(i18n::tr(i18n::StrId::ShotAlreadyTargeted));
            bump_board_dirty();
            return false;
        }
        if (!net_session_->fire(battle_cursor_)) {
            return false;
        }
        net_own_result_text_ =
            std::string(i18n::tr(i18n::StrId::NetYou)) + " " + coord_label(battle_cursor_) + ": ...";
        net_update_battle_status();
        bump_board_dirty();
        return true;
    }

    if (game_.status() != MatchStatus::InProgress) {
        return false;
    }
    if (match_mode_ == MatchMode::VsAi &&
        (ai_turn_pending() || game_.current_turn() != PlayerId::One)) {
        // The CPU's reply is still pending -- ignore human fire input rather
        // than letting the human shoot on the AI's turn.
        return false;
    }

    const auto shooter = game_.current_turn();
    const auto result = game_.shoot(shooter, battle_cursor_);
    if (!result.is_valid_input()) {
        last_shot_ = LastShotInfo{LastShotKind::Invalid, ShipType::Destroyer, shooter};
        status_text_subject_.set(i18n::tr(i18n::StrId::ShotAlreadyTargeted));
        bump_board_dirty();
        return false;
    }

    std::string result_text;
    switch (result.outcome) {
        case ShotOutcome::Miss:
            last_shot_ = LastShotInfo{LastShotKind::Miss, ShipType::Destroyer, shooter};
            result_text = i18n::tr(i18n::StrId::ShotMiss);
            break;
        case ShotOutcome::Hit:
            last_shot_ = LastShotInfo{LastShotKind::Hit, ShipType::Destroyer, shooter};
            result_text = i18n::tr(i18n::StrId::ShotHit);
            break;
        case ShotOutcome::Sunk: {
            last_shot_ = LastShotInfo{LastShotKind::Sunk, result.sunk_ship, shooter};
            result_text = i18n::trf(i18n::StrId::ShotSunk, i18n::ship_name(result.sunk_ship).c_str());
            break;
        }
        default:
            break;
    }

    if (match_mode_ == MatchMode::VsAi) {
        last_human_result_text_ = i18n::trf(i18n::StrId::BattleYouShotResult,
                                            coord_label(battle_cursor_).c_str(), result_text.c_str());
        status_text_subject_.set(last_human_result_text_.c_str());
    } else {
        status_text_subject_.set(result_text.c_str());
    }

    bump_board_dirty();

    if (match_mode_ == MatchMode::VsAi) {
        // Commentary is a VsAi-only feature (PLAN.md §2.4); hotseat matches
        // never call request_commentary(). Request AFTER bump_board_dirty()
        // so the snapshot handed to the commentator reflects this shot.
        battleship::commentary::CommentContext context;
        context.last_shot = battle_cursor_;
        context.sunk_ship = result.sunk_ship;
        switch (result.outcome) {
            case ShotOutcome::Miss:
                request_commentary(battleship::commentary::CommentEvent::HumanMiss, context);
                break;
            case ShotOutcome::Hit:
                request_commentary(battleship::commentary::CommentEvent::HumanHit, context);
                break;
            case ShotOutcome::Sunk:
                request_commentary(battleship::commentary::CommentEvent::HumanSunk, context);
                break;
            default:
                break;
        }
    }

    if (result.game_won) {
        if (match_mode_ == MatchMode::VsAi) {
            request_commentary(battleship::commentary::CommentEvent::MatchOver);
        }
        show_page(model::AppPage::Result);
    } else if (match_mode_ == MatchMode::VsAi) {
        // Stay on the Battle screen (no Handover in single-player); the CPU
        // replies automatically after a short, readable delay.
        schedule_ai_turn(kAiTurnDelayMs);
    } else {
        battle_cursor_ = Coord{0, 0};
        show_page(model::AppPage::Handover);
    }
    return true;
}

void BaseViewModel::schedule_ai_turn(uint32_t delay_ms) {
    cancel_ai_turn();
    ai_timer_ = lv_timer_create(&BaseViewModel::ai_timer_cb, delay_ms, this);
    lv_timer_set_repeat_count(ai_timer_, 1);  // one-shot; LVGL auto-deletes after firing
}

void BaseViewModel::cancel_ai_turn() {
    if (ai_timer_) {
        lv_timer_delete(ai_timer_);
        ai_timer_ = nullptr;
    }
}

void BaseViewModel::ai_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }
    // The timer is one-shot (repeat_count 1): LVGL deletes it right after
    // this callback returns, so drop our handle first -- cancel_ai_turn()
    // must never double-delete it, and ai_turn_pending() must read false
    // from inside play_ai_turn() (the human may fire again immediately).
    self->ai_timer_ = nullptr;
    self->play_ai_turn();
}

void BaseViewModel::play_ai_turn() {
    if (match_mode_ != MatchMode::VsAi || !ai_ || game_.status() != MatchStatus::InProgress ||
        game_.current_turn() != PlayerId::Two) {
        return;
    }

    const auto& tracking = game_.tracking_board(PlayerId::Two);
    const Coord target = ai_->next_shot(tracking, tracking.remaining_ship_types());
    const auto result = game_.shoot(PlayerId::Two, target);
    if (!result.is_valid_input()) {
        // Should be impossible (AiPlayer never re-fires a shot cell); guard
        // so a bug here can't wedge the match with the turn stuck on Two.
        return;
    }

    std::string cpu_result;
    switch (result.outcome) {
        case ShotOutcome::Miss:
            cpu_result = i18n::tr(i18n::StrId::ShotMiss);
            break;
        case ShotOutcome::Hit:
            cpu_result = i18n::tr(i18n::StrId::ShotHit);
            break;
        case ShotOutcome::Sunk:
            cpu_result =
                i18n::trf(i18n::StrId::ShotSunkYours, i18n::ship_name(result.sunk_ship).c_str());
            break;
        default:
            break;
    }
    const std::string cpu_text = i18n::trf(i18n::StrId::BattleCpuShotResult,
                                           coord_label(target).c_str(), cpu_result.c_str());

    // Keep the human's own last result visible alongside the CPU's reply.
    std::string status = last_human_result_text_.empty() ? cpu_text
                                                          : last_human_result_text_ + " | " + cpu_text;
    status_text_subject_.set(status.c_str());
    bump_board_dirty();

    battleship::commentary::CommentContext context;
    context.last_shot = target;
    context.sunk_ship = result.sunk_ship;
    switch (result.outcome) {
        case ShotOutcome::Miss:
            request_commentary(battleship::commentary::CommentEvent::CpuMiss, context);
            break;
        case ShotOutcome::Hit:
            request_commentary(battleship::commentary::CommentEvent::CpuHit, context);
            break;
        case ShotOutcome::Sunk:
            request_commentary(battleship::commentary::CommentEvent::CpuSunk, context);
            break;
        default:
            break;
    }

    if (result.game_won) {
        request_commentary(battleship::commentary::CommentEvent::MatchOver);
        show_page(model::AppPage::Result);
    }
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

void BaseViewModel::bump_board_dirty() {
    board_dirty_subject_.set(board_dirty_subject_.value() + 1);
}

void BaseViewModel::reset_placement_cursor() {
    placement_cursor_ = Coord{0, 0};
    placement_orientation_ = Orientation::Horizontal;
}

void BaseViewModel::request_commentary(battleship::commentary::CommentEvent event,
                                       const battleship::commentary::CommentContext& context) {
    if (!commentator_) {
        return;
    }
    // game_.snapshot() is a plain-data copy (see core/snapshot.h) safe to
    // hand off to a commentator that may forward it to a worker thread.
    commentator_->request_comment(game_.snapshot(), event, context);
}

void BaseViewModel::commentary_poll_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    std::string text;
    bool has_text = false;
    {
        std::lock_guard<std::mutex> lock(self->commentary_mutex_);
        if (self->commentary_pending_) {
            text = std::move(self->pending_commentary_);
            self->pending_commentary_.clear();
            self->commentary_pending_ = false;
            has_text = true;
        }
    }

    if (has_text && !text.empty()) {
        self->commentary_text_subject_.set(text.c_str());
    }
}

// ---------------------------------------------------------------- network

battleship::core::MatchSnapshot BaseViewModel::result_snapshot() const {
    if (match_mode_ != MatchMode::Network || !net_session_) {
        return game_.snapshot();
    }

    // Networked play has no local Game -- synthesize the two-sided snapshot
    // the Result screen expects from the single-sided PlayerView. player_one
    // is always the local player (viewing_player() convention).
    const auto& view = net_session_->player_view();
    battleship::core::MatchSnapshot snapshot;
    snapshot.status = view.status();
    snapshot.winner = view.i_won() ? PlayerId::One : PlayerId::Two;
    snapshot.current_turn = PlayerId::One;
    snapshot.turn_number = net_session_->shots_sent() + net_session_->shots_received();
    snapshot.player_one = view.snapshot();

    // The opponent's stats, as far as locally provable: every shot they
    // fired landed on our own board, so their shot count is shots_received()
    // and their hit count is the number of our damaged ship cells. Their
    // board contents are unknown by protocol design (PLAN.md §2.2) and stay
    // empty -- the Result screen only reads the counters.
    auto& opponent = snapshot.player_two;
    opponent.shots_fired = net_session_->shots_received();
    opponent.hits_recorded = 0;
    for (const auto& cell : snapshot.player_one.own_board) {
        if (cell.has_ship && cell.was_hit) {
            ++opponent.hits_recorded;
        }
    }
    return snapshot;
}

const battleship::core::Board& BaseViewModel::placement_board() const {
    if (match_mode_ == MatchMode::Network && net_session_) {
        return net_session_->player_view().own_board();
    }
    return game_.board(placement_player_);
}

battleship::core::Board& BaseViewModel::active_placement_board() {
    if (match_mode_ == MatchMode::Network && net_session_) {
        return net_session_->player_view().own_board();
    }
    return game_.board(placement_player_);
}

const battleship::core::Board& BaseViewModel::battle_own_board() const {
    if (match_mode_ == MatchMode::Network && net_session_) {
        return net_session_->player_view().own_board();
    }
    return game_.board(viewing_player());
}

const battleship::core::TrackingBoard& BaseViewModel::battle_tracking_board() const {
    if (match_mode_ == MatchMode::Network && net_session_) {
        return net_session_->player_view().tracking_board();
    }
    return game_.tracking_board(viewing_player());
}

lv_subject_t* BaseViewModel::net_status_subject() {
    return net_status_subject_.native();
}

lv_subject_t* BaseViewModel::net_hosts_dirty_subject() {
    return net_hosts_dirty_subject_.native();
}

void BaseViewModel::open_network_menu() {
    teardown_network();
    // Phase 6: Network Play starts at the connection-type screen (BLE
    // default / WiFi / LoRa placeholder), then proceeds to Host/Join.
    show_page(model::AppPage::NetTransport);
}

void BaseViewModel::select_net_transport(i18n::NetTransportPref pref) {
    if (pref == i18n::NetTransportPref::Lora && !i18n::antenna_confirmed()) {
        // One-time safety check before the first LoRa transmission: nothing
        // is persisted until the user confirms (Esc there goes back here).
        show_page(model::AppPage::NetAntennaConfirm);
        return;
    }
    // Persist immediately so this choice preselects next time (PLAN.md
    // Phase 6 "選択は設定に永続化（次回デフォルト）").
    i18n::save_net_transport_pref(pref);
    show_page(model::AppPage::NetworkMenu);
}

void BaseViewModel::confirm_lora_antenna() {
    i18n::save_antenna_confirmed(true);
    i18n::save_net_transport_pref(i18n::NetTransportPref::Lora);
    show_page(model::AppPage::NetworkMenu);
}

void BaseViewModel::start_network_host() {
    teardown_network();

    if (net_using_lora()) {
        if (!net_lora_open()) {
            return;  // NetError already shown, radio/power released
        }
        const std::string host_name = battleship::net::local_host_name();
        // The beacon itself carries the host name (truncated to 12 chars by
        // the packet codec); the lobby shows the same "BSHIP-" form BLE uses.
        net_lora_link_->start_host(host_name, net_lora_now_ms());
        net_lora_transport_ = std::make_unique<battleship::net::lora::LoRaTransport>(
            *net_lora_link_, &BaseViewModel::net_lora_now_ms);

        net_is_host_ = true;
        net_session_ = std::make_unique<battleship::net::NetSession>(
            *net_lora_transport_, battleship::net::SessionRole::Host, rng_(),
            battleship::net::lora::lora_session_config());
        net_seen_change_counter_ = 0;
        net_seen_phase_ = battleship::net::SessionPhase::Handshake;

        const std::string advertised = std::string(battleship::net::kBleNamePrefix) + host_name;
        net_status_subject_.set(
            i18n::trf(i18n::StrId::NetLoraHostingAs, advertised.c_str()).c_str());

        if (!net_poll_timer_) {
            net_poll_timer_ =
                lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
        }
        show_page(model::AppPage::NetLobby);
        return;
    }

#if BATTLESHIP_BLE
    if (net_using_ble()) {
        net_flow_ble_ = true;
        net_ble_transport_ = std::make_unique<battleship::net::BleTransport>();
        const std::string host_name = battleship::net::local_host_name();
        if (!net_ble_transport_->start_host(host_name)) {
            net_ble_transport_.reset();
            net_flow_ble_ = false;
            net_status_subject_.set(i18n::tr(i18n::StrId::NetBleUnavailable));
            show_page(model::AppPage::NetError);
            return;
        }

        net_is_host_ = true;
        net_session_ = std::make_unique<battleship::net::NetSession>(
            *net_ble_transport_, battleship::net::SessionRole::Host, rng_());
        net_seen_change_counter_ = 0;
        net_seen_phase_ = battleship::net::SessionPhase::Handshake;

        const std::string advertised = std::string(battleship::net::kBleNamePrefix) + host_name;
        net_status_subject_.set(
            i18n::trf(i18n::StrId::NetLobbyAdvertisingAs, advertised.c_str()).c_str());

        if (!net_poll_timer_) {
            net_poll_timer_ =
                lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
        }
        show_page(model::AppPage::NetLobby);
        return;
    }
#endif

    net_transport_ = std::make_unique<battleship::net::TcpTransport>();
    // Preferred fixed port first (predictable for Direct IP joins); fall
    // back to an OS-assigned one when it is taken -- notably the second
    // instance during 2-process testing on one machine. Discovery announces
    // whichever port was actually bound.
    if (!net_transport_->listen(kNetGamePort) && !net_transport_->listen(0)) {
        net_transport_.reset();
        net_status_subject_.set(i18n::tr(i18n::StrId::NetCouldNotOpenPort));
        show_page(model::AppPage::NetError);
        return;
    }

    net_is_host_ = true;
    net_session_ = std::make_unique<battleship::net::NetSession>(
        *net_transport_, battleship::net::SessionRole::Host, rng_());
    net_seen_change_counter_ = 0;
    net_seen_phase_ = battleship::net::SessionPhase::Handshake;

    net_announcing_ = net_announcer_.start(battleship::net::local_host_name(),
                                           net_transport_->local_port());

    net_status_subject_.set(i18n::trf(i18n::StrId::NetLobbyYourAddress,
                                      battleship::net::local_ipv4_address().c_str(),
                                      static_cast<unsigned>(net_transport_->local_port()))
                                .c_str());

    if (!net_poll_timer_) {
        net_poll_timer_ = lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
    }
    show_page(model::AppPage::NetLobby);
}

void BaseViewModel::start_network_join() {
    teardown_network();

    net_is_host_ = false;

    if (net_using_lora()) {
        if (!net_lora_open()) {
            return;  // NetError already shown, radio/power released
        }
        net_lora_link_->start_scan(net_lora_now_ms());
        net_hosts_signature_ = 0;
        net_status_subject_.set(i18n::tr(i18n::StrId::NetJoinScanningLora));
        if (!net_poll_timer_) {
            net_poll_timer_ =
                lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
        }
        net_hosts_dirty_subject_.set(net_hosts_dirty_subject_.value() + 1);
        show_page(model::AppPage::NetJoin);
        return;
    }

#if BATTLESHIP_BLE
    if (net_using_ble()) {
        net_flow_ble_ = true;
        net_ble_scanning_ = net_ble_scanner_.start();
        net_hosts_signature_ = 0;
        net_status_subject_.set(i18n::tr(net_ble_scanning_ ? i18n::StrId::NetJoinScanningBle
                                                           : i18n::StrId::NetBleUnavailable));
        if (!net_poll_timer_) {
            net_poll_timer_ =
                lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
        }
        net_hosts_dirty_subject_.set(net_hosts_dirty_subject_.value() + 1);
        show_page(model::AppPage::NetJoin);
        return;
    }
#endif

    net_discovering_ = net_discoverer_.start();
    net_hosts_signature_ = 0;
    net_status_subject_.set(i18n::tr(net_discovering_ ? i18n::StrId::NetJoinSearching
                                                        : i18n::StrId::NetJoinDiscoveryUnavailable));
    if (!net_poll_timer_) {
        net_poll_timer_ = lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
    }
    net_hosts_dirty_subject_.set(net_hosts_dirty_subject_.value() + 1);
    show_page(model::AppPage::NetJoin);
}

size_t BaseViewModel::net_peer_count() const {
    if (net_flow_lora_) {
        return net_lora_link_ ? net_lora_link_->peers().size() : 0;
    }
#if BATTLESHIP_BLE
    if (net_flow_ble_) {
        return net_ble_scanner_.results().size();
    }
#endif
    return net_discoverer_.hosts().size();
}

std::string BaseViewModel::net_peer_label(size_t index) const {
    char text[96];
    if (net_flow_lora_) {
        if (!net_lora_link_ || index >= net_lora_link_->peers().size()) {
            return {};
        }
        const auto& peer = net_lora_link_->peers()[index];
        std::snprintf(text, sizeof(text), "%s  %d dBm", peer.host_name.c_str(),
                      static_cast<int>(peer.rssi_dbm));
        return text;
    }
#if BATTLESHIP_BLE
    if (net_flow_ble_) {
        const auto& results = net_ble_scanner_.results();
        if (index >= results.size()) {
            return {};
        }
        std::snprintf(text, sizeof(text), "%s (%s)", results[index].name.c_str(),
                      results[index].address.c_str());
        return text;
    }
#endif
    const auto& hosts = net_discoverer_.hosts();
    if (index >= hosts.size()) {
        return {};
    }
    std::snprintf(text, sizeof(text), "%s (%s:%u)", hosts[index].name.c_str(),
                  hosts[index].ip.c_str(), static_cast<unsigned>(hosts[index].tcp_port));
    return text;
}

bool BaseViewModel::net_connect_to(const std::string& ip, uint16_t port) {
    net_transport_ = std::make_unique<battleship::net::TcpTransport>();
    if (!net_transport_->connect(ip, port)) {
        net_transport_.reset();
        net_status_subject_.set(i18n::tr(i18n::StrId::NetCouldNotStartConnection));
        return false;
    }

    net_discoverer_.stop();
    net_discovering_ = false;
    net_is_host_ = false;
    net_session_ = std::make_unique<battleship::net::NetSession>(
        *net_transport_, battleship::net::SessionRole::Client, rng_());
    net_seen_change_counter_ = 0;
    net_seen_phase_ = battleship::net::SessionPhase::Handshake;

    net_status_subject_.set(i18n::trf(i18n::StrId::NetLobbyConnectingTo, ip.c_str(),
                                      static_cast<unsigned>(port))
                                .c_str());

    if (!net_poll_timer_) {
        net_poll_timer_ = lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
    }
    show_page(model::AppPage::NetLobby);
    return true;
}

void BaseViewModel::join_host_at(size_t index) {
    if (net_flow_lora_) {
        if (!net_lora_link_ || net_session_ || index >= net_lora_link_->peers().size()) {
            return;
        }
        // Copy: the peers vector belongs to the link and changes on poll.
        const auto peer = net_lora_link_->peers()[index];
        if (!net_lora_link_->join(peer.session_id, net_lora_now_ms())) {
            net_status_subject_.set(i18n::tr(i18n::StrId::NetCouldNotStartConnection));
            return;  // stay on the join list; user can retry/cancel
        }
        net_lora_transport_ = std::make_unique<battleship::net::lora::LoRaTransport>(
            *net_lora_link_, &BaseViewModel::net_lora_now_ms);
        net_is_host_ = false;
        net_session_ = std::make_unique<battleship::net::NetSession>(
            *net_lora_transport_, battleship::net::SessionRole::Client, rng_(),
            battleship::net::lora::lora_session_config());
        net_seen_change_counter_ = 0;
        net_seen_phase_ = battleship::net::SessionPhase::Handshake;

        net_status_subject_.set(
            i18n::trf(i18n::StrId::NetLobbyConnectingToBle, peer.host_name.c_str()).c_str());

        if (!net_poll_timer_) {
            net_poll_timer_ =
                lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
        }
        show_page(model::AppPage::NetLobby);
        return;
    }
#if BATTLESHIP_BLE
    if (net_flow_ble_) {
        const auto& results = net_ble_scanner_.results();
        if (index >= results.size()) {
            return;
        }
        // Copy before stopping the scanner -- stop() clears the results
        // vector this element lives in.
        const auto peer = results[index];
        net_ble_scanner_.stop();
        net_ble_scanning_ = false;

        net_ble_transport_ = std::make_unique<battleship::net::BleTransport>();
        if (!net_ble_transport_->start_join(peer.device_path)) {
            net_ble_transport_.reset();
            net_status_subject_.set(i18n::tr(i18n::StrId::NetCouldNotStartConnection));
            return;  // stay on the scan screen; user can retry/cancel
        }
        net_is_host_ = false;
        net_session_ = std::make_unique<battleship::net::NetSession>(
            *net_ble_transport_, battleship::net::SessionRole::Client, rng_());
        net_seen_change_counter_ = 0;
        net_seen_phase_ = battleship::net::SessionPhase::Handshake;

        net_status_subject_.set(
            i18n::trf(i18n::StrId::NetLobbyConnectingToBle, peer.name.c_str()).c_str());

        if (!net_poll_timer_) {
            net_poll_timer_ =
                lv_timer_create(&BaseViewModel::net_poll_timer_cb, kNetPollIntervalMs, this);
        }
        show_page(model::AppPage::NetLobby);
        return;
    }
#endif

    const auto& hosts = net_discoverer_.hosts();
    if (index >= hosts.size()) {
        return;
    }
    // Copy before net_connect_to() -- it stops the discoverer, which clears
    // the hosts vector this element lives in.
    const auto host = hosts[index];
    net_connect_to(host.ip, host.tcp_port);
}

bool BaseViewModel::join_direct(const char* text) {
    if (!text) {
        return false;
    }

    // Accept "ip" or "ip:port"; tolerate surrounding whitespace.
    std::string spec(text);
    const auto first = spec.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return false;
    }
    const auto last = spec.find_last_not_of(" \t\r\n");
    spec = spec.substr(first, last - first + 1);

    std::string ip = spec;
    uint16_t port = kNetGamePort;
    const auto colon = spec.find(':');
    if (colon != std::string::npos) {
        ip = spec.substr(0, colon);
        try {
            const int parsed = std::stoi(spec.substr(colon + 1));
            if (parsed <= 0 || parsed > 65535) {
                return false;
            }
            port = static_cast<uint16_t>(parsed);
        } catch (const std::exception&) {
            return false;
        }
    }
    if (ip.empty()) {
        return false;
    }
    return net_connect_to(ip, port);
}

void BaseViewModel::cancel_network() {
    teardown_network();
    show_page(model::AppPage::NetworkMenu);
}

void BaseViewModel::teardown_network() {
    if (net_poll_timer_) {
        lv_timer_delete(net_poll_timer_);
        net_poll_timer_ = nullptr;
    }
    net_announcer_.stop();
    net_discoverer_.stop();
    net_announcing_ = false;
    net_discovering_ = false;
    net_session_.reset();
    net_transport_.reset();
    // LoRa after the session (which references the transport).
    net_lora_begin_close();
    net_flow_lora_ = false;
    net_lora_seen_state_ = battleship::net::lora::LinkState::Idle;
#if BATTLESHIP_BLE
    net_ble_scanner_.stop();
    net_ble_scanning_ = false;
    net_ble_transport_.reset();
#endif
    net_flow_ble_ = false;
    net_own_result_text_.clear();
    net_incoming_result_text_.clear();
    net_hosts_signature_ = 0;
    net_autopilot_placed_ = 0;
    net_autopilot_tick_ = 0;
    if (match_mode_ == MatchMode::Network) {
        // Leave a safe default behind so screens that consult match_mode()
        // outside a network flow never see Network with a null session.
        match_mode_ = MatchMode::Hotseat;
    }
}

uint64_t BaseViewModel::net_lora_now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool BaseViewModel::net_lora_open() {
    using battleship::net::lora::RadioInitError;
    // Never two radios at once: a previous flow's BYE drain ends here.
    net_lora_finish_closing();
    release_radio(net_lora_radio_);

    auto handle = battleship::net::lora::open_radio(battleship::net::lora::default_radio_backend());
    if (!handle.radio || !handle.radio->ready()) {
        // open_radio() has already released any power it switched on when it
        // returns no radio; a radio object that is not ready is destroyed
        // here (radio first, then power).
        const RadioInitError error =
            handle.error != RadioInitError::None ? handle.error : RadioInitError::ConfigFailed;
        release_radio(handle);
        std::printf("[lora] radio open failed (error %d)\n", static_cast<int>(error));
        net_lora_show_error(lora_error_message(error));
        return false;
    }
    std::printf("[lora] radio opened (%s)\n",
                battleship::net::lora::default_radio_backend() ==
                        battleship::net::lora::RadioBackend::Hardware
                    ? "hardware"
                    : "udp-sim");
    net_lora_radio_ = std::move(handle);  // target is empty: nothing destroyed
    net_lora_link_ = std::make_unique<battleship::net::lora::LoRaLink>(
        *net_lora_radio_.radio, battleship::net::lora::Japan920Policy{}, rng_());
    net_lora_seen_state_ = battleship::net::lora::LinkState::Idle;
    net_flow_lora_ = true;
    return true;
}

void BaseViewModel::net_lora_show_error(i18n::StrId message) {
    net_status_subject_.set(i18n::tr(message));
    show_page(model::AppPage::NetError);
}

void BaseViewModel::net_lora_begin_close() {
    using battleship::net::lora::LinkState;
    if (!net_lora_link_) {
        net_lora_transport_.reset();
        release_radio(net_lora_radio_);
        return;
    }
    const uint64_t now = net_lora_now_ms();
    const bool was_connected = net_lora_link_->state() == LinkState::Connected;
    if (was_connected) {
        // Give frames the session queued just before teardown (e.g. the
        // GAME_OVER from NetSession::abandon) one chance to start on air;
        // close() then drops anything still queued and queues the BYE.
        net_lora_link_->poll(now);
    }
    net_lora_link_->close(now);

    if (!was_connected || !net_lora_radio_.radio || !net_lora_radio_.radio->ready()) {
        // No BYE pending (or no radio left to send it): destroy right away.
        net_lora_transport_.reset();
        net_lora_link_.reset();
        release_radio(net_lora_radio_);
        return;
    }

    net_lora_finish_closing();  // at most one draining set at a time
    net_lora_closing_ = std::make_unique<LoraClosing>();
    net_lora_closing_->radio = std::move(net_lora_radio_);  // target empty
    net_lora_closing_->link = std::move(net_lora_link_);
    net_lora_closing_->transport = std::move(net_lora_transport_);
    net_lora_closing_->deadline_ms = now + kLoraCloseDrainMs;
    net_lora_closing_timer_ =
        lv_timer_create(&BaseViewModel::net_lora_closing_timer_cb, kLoraClosePollMs, this);
}

void BaseViewModel::net_lora_finish_closing() {
    if (net_lora_closing_timer_) {
        lv_timer_delete(net_lora_closing_timer_);
        net_lora_closing_timer_ = nullptr;
    }
    if (net_lora_closing_) {
        net_lora_closing_.reset();  // transport -> link -> radio -> Cap power
        std::printf("[lora] radio released (after BYE drain)\n");
    }
}

void BaseViewModel::net_lora_closing_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (!self || !self->net_lora_closing_) {
        return;
    }
    const uint64_t now = net_lora_now_ms();
    if (now >= self->net_lora_closing_->deadline_ms) {
        self->net_lora_finish_closing();  // deletes this timer (safe in LVGL 9)
        return;
    }
    self->net_lora_closing_->link->poll(now);
}

void BaseViewModel::net_poll_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->net_poll();
    }
}

void BaseViewModel::net_poll() {
    const uint32_t now = lv_tick_get();

    if (net_announcing_) {
        net_announcer_.poll(now);
    }
    if (net_discovering_) {
        net_discoverer_.poll(now);
        // Cheap change detection so the Join screen only rebuilds its list
        // when something actually changed.
        size_t signature = net_discoverer_.hosts().size() * 1000003u;
        for (const auto& host : net_discoverer_.hosts()) {
            signature = signature * 31u + std::hash<std::string>{}(host.ip) + host.tcp_port;
        }
        if (signature != net_hosts_signature_) {
            net_hosts_signature_ = signature;
            net_hosts_dirty_subject_.set(net_hosts_dirty_subject_.value() + 1);
        }
    }
#if BATTLESHIP_BLE
    if (net_ble_scanning_) {
        net_ble_scanner_.poll(now);
        // Same cheap change detection as WiFi discovery above, over the BLE
        // scan results (device_path is the stable identity key).
        size_t signature = net_ble_scanner_.results().size() * 1000003u;
        for (const auto& result : net_ble_scanner_.results()) {
            signature = signature * 31u + std::hash<std::string>{}(result.device_path);
        }
        if (signature != net_hosts_signature_) {
            net_hosts_signature_ = signature;
            net_hosts_dirty_subject_.set(net_hosts_dirty_subject_.value() + 1);
        }
    }
#endif
    if (net_lora_link_ && !net_session_) {
        // Scanning (no session yet): drive the link directly. Once a session
        // exists, NetSession::poll drives it through LoRaTransport::poll.
        net_lora_link_->poll(net_lora_now_ms());
    }
    if (net_lora_link_ && net_lora_link_->state() == battleship::net::lora::LinkState::Scanning) {
        // Same change detection as above over the LoRa beacon peers. RSSI is
        // part of the label, so it is part of the signature too (the Join
        // screen keeps keyboard focus on the same row across rebuilds).
        const auto& peers = net_lora_link_->peers();
        size_t signature = peers.size() * 1000003u;
        for (const auto& peer : peers) {
            signature = signature * 31u + std::hash<std::string>{}(peer.host_name) +
                        peer.session_id * 131u + static_cast<size_t>(peer.rssi_dbm + 200);
        }
        if (signature != net_hosts_signature_) {
            net_hosts_signature_ = signature;
            net_hosts_dirty_subject_.set(net_hosts_dirty_subject_.value() + 1);
        }
    }
    if (net_session_) {
        net_session_->poll(now);
    }
    if (net_lora_link_) {
        using battleship::net::lora::LinkState;
        const LinkState state = net_lora_link_->state();
        if (state == LinkState::Failed) {
            // Join attempts exhausted (radio still fine) reads as a failed
            // connection attempt; anything else is a radio/policy failure.
            const bool join_gave_up = net_lora_seen_state_ == LinkState::Joining &&
                                      net_lora_radio_.radio && net_lora_radio_.radio->ready();
            teardown_network();  // releases radio + Cap power (no BYE: not Connected)
            net_lora_show_error(join_gave_up ? i18n::StrId::NetCouldNotStartConnection
                                             : i18n::StrId::NetLoraUnavailable);
            return;
        }
        net_lora_seen_state_ = state;
    }
    if (net_session_) {
        net_sync_session_state();
        if (net_autopilot_mode_ != 0) {
            net_autopilot_step();
        }
    }
}

void BaseViewModel::net_autopilot_step() {
    using battleship::net::SessionPhase;
    if (!net_session_) {
        return;
    }

    if (net_session_->phase() == SessionPhase::Placement) {
        // Deterministic layout, one ship per poll tick (so the placement
        // screen is actually observable in screenshots): each ship
        // horizontal at (0, row), rows 0-4 -- every ship cell inside the
        // x0-4/y0-4 block the "win" sweep covers.
        auto& board = active_placement_board();
        if (++net_autopilot_tick_ % 3 != 0) {
            return;  // one placement action per ~300ms so screenshots can see it
        }
        if (net_autopilot_placed_ < battleship::core::kShipTypeCount) {
            const auto type = kAllShipTypes[net_autopilot_placed_];
            board.place_ship(battleship::core::Ship(
                type, Coord{0, net_autopilot_placed_}, Orientation::Horizontal));
            ++net_autopilot_placed_;
            bump_board_dirty();
            return;
        }
        if (board.is_fully_placed()) {
            advance_from_placement();  // sends READY, moves to the battle page
        }
        return;
    }

    if (net_session_->phase() != SessionPhase::Battle || !net_session_->can_fire()) {
        return;
    }

    const auto& tracking = net_session_->player_view().tracking_board();
    auto try_fire_first_unshot = [&](int x0, int x1, int y0, int y1) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const Coord cell{x, y};
                if (!tracking.was_shot_at(cell)) {
                    battle_cursor_ = cell;
                    return fire_at_cursor();
                }
            }
        }
        return false;
    };

    if (net_autopilot_mode_ == 1) {
        // "win": clear the block both autopilot fleets occupy, then (safety
        // net -- should never be reached when the peer also autopilots) the
        // rest of the board.
        if (!try_fire_first_unshot(0, 4, 0, 4)) {
            try_fire_first_unshot(0, battleship::core::kBoardSize - 1, 0,
                                  battleship::core::kBoardSize - 1);
        }
    } else {
        // "lose": only ever fire into rows the autopilot fleets never touch,
        // so this side can reply every turn yet cannot win.
        try_fire_first_unshot(0, battleship::core::kBoardSize - 1, 5,
                              battleship::core::kBoardSize - 1);
    }
}

void BaseViewModel::net_sync_session_state() {
    using battleship::net::SessionPhase;
    if (!net_session_) {
        return;
    }

    const auto phase = net_session_->phase();
    const uint32_t counter = net_session_->change_counter();
    const bool changed = counter != net_seen_change_counter_;
    const auto previous_phase = net_seen_phase_;
    if (!changed && phase == previous_phase) {
        return;
    }
    net_seen_change_counter_ = counter;
    net_seen_phase_ = phase;

    if (phase != previous_phase) {
        switch (phase) {
            case SessionPhase::Placement:
                if (previous_phase == SessionPhase::Handshake) {
                    // Connection up + HELLO exchanged: the match begins.
                    // Stop advertising/scanning (host must vanish from other
                    // players' Join lists once paired).
                    net_announcer_.stop();
                    net_announcing_ = false;
                    net_discoverer_.stop();
                    net_discovering_ = false;

                    cancel_ai_turn();
                    match_mode_ = MatchMode::Network;
                    placement_player_ = PlayerId::One;
                    reset_placement_cursor();
                    battle_cursor_ = Coord{0, 0};
                    last_shot_ = LastShotInfo{};
                    net_own_result_text_.clear();
                    net_incoming_result_text_.clear();
                    status_text_subject_.set("");
                    commentary_text_subject_.set("");
                    show_page(model::AppPage::Placement);
                    bump_board_dirty();
                }
                break;
            case SessionPhase::Battle:
                net_update_battle_status();
                bump_board_dirty();
                break;
            case SessionPhase::Finished:
                net_update_battle_status();
                show_page(model::AppPage::Result);
                bump_board_dirty();
                break;
            case SessionPhase::Disconnected:
                if (previous_phase != SessionPhase::Finished) {
                    net_status_subject_.set(i18n::tr(i18n::StrId::NetErrorConnectionLost));
                    show_page(model::AppPage::NetError);
                }
                // Post-game transport death (opponent quit the app from
                // their result screen) is normal -- stay on Result silently.
                break;
            default:
                break;
        }
        return;
    }

    // Same phase, new events (a shot resolved, opponent fired, peer ready).
    if (phase == SessionPhase::Battle || phase == SessionPhase::WaitPeerReady) {
        net_update_battle_status();
        bump_board_dirty();
    }
}

void BaseViewModel::net_update_battle_status() {
    using battleship::net::SessionPhase;
    if (!net_session_) {
        return;
    }
    const auto& view = net_session_->player_view();

    if (const auto& own = net_session_->last_own_shot_result()) {
        net_own_result_text_ =
            net_shot_text(i18n::StrId::NetYou, own->target, own->result, /*own_shot=*/true);
    }
    if (const auto& incoming = net_session_->last_incoming_shot()) {
        net_incoming_result_text_ =
            net_shot_text(i18n::StrId::NetFoe, incoming->target, incoming->result, /*own_shot=*/false);
    }

    std::string status = net_own_result_text_;
    if (!net_incoming_result_text_.empty()) {
        if (!status.empty()) {
            status += " | ";
        }
        status += net_incoming_result_text_;
    }
    status_text_subject_.set(status.c_str());

    // Turn indicator + confirmed-sunk list. NOTES.md §6.4: in networked play
    // the tracking board cannot retroactively repaint a sunk ship's earlier
    // Hit cells (the opponent's board never exists locally), so the
    // authoritative sunk_ships() list is surfaced textually here instead.
    std::string line;
    switch (net_session_->phase()) {
        case SessionPhase::WaitPeerReady:
            line = i18n::tr(i18n::StrId::NetWaitingOpponentPlacing);
            break;
        case SessionPhase::Battle:
            if (net_session_->can_fire()) {
                line = i18n::tr(i18n::StrId::NetYourTurnFire);
            } else if (net_session_->pending_shot()) {
                line = i18n::tr(i18n::StrId::NetShotFiredWaiting);
            } else {
                line = i18n::tr(i18n::StrId::NetWaitingOpponentAttack);
            }
            break;
        case SessionPhase::Finished:
            line = i18n::tr(view.i_won() ? i18n::StrId::NetVictory : i18n::StrId::NetDefeat);
            break;
        default:
            break;
    }
    const auto& sunk = view.tracking_board().sunk_ships();
    if (!sunk.empty()) {
        line += i18n::tr(i18n::StrId::NetEnemySunkPrefix);
        for (size_t i = 0; i < sunk.size(); ++i) {
            if (i > 0) {
                line += ", ";
            }
            line += i18n::ship_name(sunk[i]);
        }
    }
    commentary_text_subject_.set(line.c_str());
}

} // namespace viewmodel
