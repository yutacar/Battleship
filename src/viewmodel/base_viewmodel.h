/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * GameViewModel (kept as class name BaseViewModel for minimal churn against
 * the template's ScreenManager/BaseScreen plumbing): the single ViewModel
 * instance shared by every screen. Wraps a battleship::core::Game and
 * exposes:
 *   - LVGL subjects for screen navigation (current_page_subject) and simple
 *     cross-cutting UI state (dark_mode, quit_requested).
 *   - Placement-phase helpers (cursor movement, rotate, commit/random/undo)
 *     operated one player at a time (placement_player()).
 *   - Battle-phase helpers (cursor movement over the tracking board, fire).
 *   - Read-only accessors screens use to pull Board/TrackingBoard/Snapshot
 *     state each redraw; Phase 2 favors simple polling from screen update
 *     callbacks over a fully reactive per-cell subject grid (100+ subjects
 *     per board would be excessive for a 10x10 grid on this hardware).
 *
 * This is intentionally a single "god" viewmodel rather than one per screen:
 * PLAN.md's hotseat flow is a strict linear state machine (Menu ->
 * Placement(P1) -> Placement(P2) -> Handover -> Battle -> ... -> Result) and
 * every screen after Menu needs access to the same underlying Game, so
 * splitting it up would just mean passing the same Game reference around
 * multiple objects for no benefit at this phase. Phase 3 (AI) and Phase 4
 * (networked play) are expected to introduce an IOpponent-shaped seam
 * (see request_shot_for_ai()-style hook noted in NOTES.md) without needing
 * to restructure this class.
 */
#pragma once

#include "subjects.h"
#include "base_model.h"

#include "commentary/commentator.h"
#include "core/battleship_core.h"
#include "i18n/settings.h"
#include "i18n/strings.h"
#include "net/lora/lora_link.h"
#include "net/lora/lora_transport.h"
#include "net/lora/radio_factory.h"
#include "net/net_session.h"
#if BATTLESHIP_BLE
#include "net/ble/ble_transport.h"
#endif

#include "lvgl.h"

#include <memory>
#include <mutex>
#include <random>
#include <string>

namespace viewmodel {

/// Who controls player Two of the current match.
enum class MatchMode {
    Hotseat,  ///< Two humans sharing the device (Phase 2 flow, with Handover).
    VsAi,     ///< Human (player One) vs AiPlayer (player Two); no Handover.
    Network,  ///< Remote human over BLE or LoRa. Match state lives in
              ///< a net::NetSession + core::PlayerView instead of core::Game.
};

/// Result of the most recent shot fired during the Battle phase, used to
/// populate the status line ("Hit!", "Miss", "Sunk: Destroyer!", ...).
enum class LastShotKind {
    None,
    Miss,
    Hit,
    Sunk,
    Invalid,  ///< already-shot cell tapped again; no state change happened.
};

struct LastShotInfo {
    LastShotKind kind = LastShotKind::None;
    battleship::core::ShipType sunk_ship = battleship::core::ShipType::Destroyer;
    battleship::core::PlayerId shooter = battleship::core::PlayerId::One;
};

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    // ---- Cross-cutting subjects ----
    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();
    /// Bumped (int always increasing) whenever set_language() runs, so
    /// ScreenManager can observe it and rebuild the current screen (see
    /// ScreenManager::reload_current_screen()) -- current_page_subject()
    /// alone doesn't fire again when the page value is unchanged.
    lv_subject_t* language_version_subject();
    /// Bumped (int always increasing) whenever placement/battle board state
    /// changes so screens' board-redraw observers can subscribe to a single
    /// subject instead of polling every frame.
    lv_subject_t* board_dirty_subject();
    /// Human-readable status line text for the Battle screen (updated after
    /// every shot) -- e.g. "Hit!", "Miss", "Sunk: Destroyer!".
    lv_subject_t* status_text_subject();
    /// Commentator (Phase 3b) output line, separate from status_text_subject()
    /// so shot-result text and LLM/template commentary don't fight over the
    /// same line (see NOTES.md §8.5). Empty until the first comment arrives;
    /// screens should simply not render this line while empty. Only
    /// meaningful in VsAi mode -- hotseat matches never populate it.
    lv_subject_t* commentary_text_subject();
    /// True while the "Abandon match?" confirmation overlay (App Submission
    /// Guide short-Esc requirement, PLAN.md Phase 7) is showing. ScreenManager
    /// observes this to add/remove the overlay on top of whatever screen is
    /// currently loaded, independent of AppPage navigation.
    lv_subject_t* interrupt_modal_subject();
    /// True for a short window after the first Esc on the title (Menu):
    /// a second Esc inside that window exits to the launcher. The Menu
    /// screen observes this to swap its "Esc x2: Quit" hint for "Press Esc
    /// again to quit". Driven by ScreenManager.
    lv_subject_t* title_quit_armed_subject();
    void set_title_quit_armed(bool armed);
    bool title_quit_armed() const;

    bool is_dark_mode() const;
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void request_quit();

    // ---- Esc key convention (PLAN.md Phase 7 "Esc キー規約") ----
    /// True while a match is actually in progress and worth confirming
    /// before abandoning: Battle/Placement/Handover screens with a hotseat
    /// or VsAi game that hasn't reached MatchStatus::Finished yet, or a
    /// network session that hasn't reached Finished/Disconnected. Result,
    /// Menu, and every setup/menu screen return false -- short Esc there
    /// just navigates back without a confirmation prompt.
    bool is_match_interruptible() const;
    /// Shows the "Abandon match?" confirmation overlay. No-op if already
    /// showing.
    void request_interrupt_confirm();
    /// Hides the confirmation overlay without abandoning the match (short
    /// Esc again, or a "No"/Cancel button on the modal).
    void dismiss_interrupt_confirm();
    /// User confirmed "Yes" on the abandon-match overlay: for a network
    /// match, best-effort notifies the peer (NetSession::abandon(), see
    /// net/net_session.h) so it doesn't sit waiting for the 10s
    /// PeerSilent disconnect timeout, then tears down all match/network
    /// state and returns to the title (Menu), same as Result's "Title"
    /// button. Hides the overlay as a side effect.
    void confirm_interrupt();
    bool interrupt_modal_visible() const { return interrupt_modal_visible_; }
    /// Safe exit from anywhere (long Esc / Home, App Submission Guide):
    /// best-effort notifies a connected peer (NetSession::abandon), tears
    /// down all network state, then requests a clean main-loop exit via
    /// quit_requested_subject(). Settings need no flush here -- they are
    /// persisted the moment they change (i18n::save_language /
    /// save_net_transport_pref).
    void request_safe_quit();

    // ---- Language (Phase 5 i18n) ----
    /// Current UI language (mirrors i18n::current_language(), which is a
    /// process-global -- this accessor exists so screens don't need to
    /// #include i18n/strings.h just to read it back off the view model).
    battleship::commentary::Lang language() const;
    /// Applies `lang` immediately (i18n::set_language + persists to
    /// ~/.config/battleship/settings.conf via i18n::save_language),
    /// propagates it to the commentator (LLM prompt / template phrase
    /// language, PLAN.md Phase 5 "LLM 解説との連携"), and forces the
    /// currently-visible screen to rebuild so every label re-reads tr()
    /// immediately (PLAN.md: "選択即反映"). Safe to call with the
    /// already-active language (re-shows the current page harmlessly).
    void set_language(battleship::commentary::Lang lang);

    // ---- Match lifecycle ----
    battleship::core::Game& game() { return game_; }
    const battleship::core::Game& game() const { return game_; }

    /// Resets the match (both boards cleared) and returns to Placement for
    /// player One, keeping the current MatchMode + AI difficulty. Called
    /// from Result's "rematch" (and internally by the two mode-specific
    /// starters below).
    void start_new_match();

    /// Starts a fresh 2-human hotseat match (Menu "2 Player").
    void start_hotseat_match();
    /// Starts a fresh human-vs-AI match at `difficulty` (Difficulty screen).
    /// Player One is the human, player Two the AI; the AI's fleet is placed
    /// automatically when the human confirms their placement.
    void start_ai_match(battleship::core::ai::Difficulty difficulty);

    MatchMode match_mode() const { return match_mode_; }
    battleship::core::ai::Difficulty ai_difficulty() const { return ai_difficulty_; }

    /// The player whose boards the Battle screen should render: always One
    /// (the human) in VsAi mode -- even while the AI's shot is pending --
    /// and the current-turn player in hotseat mode.
    battleship::core::PlayerId viewing_player() const;

    // ---- Placement phase ----
    /// Which player is currently placing ships (One during the first
    /// placement screen, Two during the second).
    battleship::core::PlayerId placement_player() const { return placement_player_; }
    /// Advances from placing player One to Two, or (if Two just finished)
    /// starts the match and moves to the Handover screen ahead of Battle.
    void advance_from_placement();

    /// Ship type currently being placed (first not-yet-placed type in
    /// kAllShipTypes order), or nullopt-like sentinel via has_ship_to_place().
    bool has_ship_to_place() const;
    battleship::core::ShipType next_ship_to_place() const;

    battleship::core::Coord placement_cursor() const { return placement_cursor_; }
    battleship::core::Orientation placement_orientation() const { return placement_orientation_; }
    void move_placement_cursor(int dx, int dy);
    void toggle_placement_orientation();
    /// True if the ship-to-place would fit at the current cursor/orientation
    /// without going out of bounds or overlapping an already-placed ship.
    bool is_placement_preview_valid() const;
    /// Attempts to place the current ship at the cursor. No-op if invalid.
    /// Returns true if a ship was placed.
    bool commit_placement();
    /// Removes the most recently placed ship (if any) so it can be
    /// repositioned. Returns true if a ship was removed.
    bool undo_last_placement();
    /// Clears the current player's board and randomly places the full fleet.
    void randomize_placement();

    // ---- Handover phase ----
    /// Player who is about to play next (i.e. whose board must be hidden by
    /// the handover screen before Battle resumes).
    battleship::core::PlayerId upcoming_player() const;
    void confirm_handover();

    // ---- Battle phase ----
    battleship::core::Coord battle_cursor() const { return battle_cursor_; }
    void move_battle_cursor(int dx, int dy);
    /// Fires at the current battle_cursor() cell on behalf of
    /// game().current_turn(). No-op (returns false) if that cell was
    /// already targeted. On a game-ending shot, transitions to Result.
    /// Otherwise transitions to Handover (turn passed to the other player)
    /// unless keep_same_player_view is needed by a future AI hook.
    bool fire_at_cursor();
    const LastShotInfo& last_shot() const { return last_shot_; }

    /// True while the AI's turn is scheduled/being played (VsAi mode only);
    /// human fire input is ignored during this window.
    bool ai_turn_pending() const { return ai_timer_ != nullptr; }

    // ---- Result phase ----
    /// Snapshot for the Result screen. Hotseat/VsAi: the full Game snapshot.
    /// Network: synthesized from the local PlayerView -- player_one is the
    /// local player; player_two's stats are reconstructed from what the
    /// opponent's shots revealed locally (their per-cell tracking view of us
    /// equals our own board's shot history).
    battleship::core::MatchSnapshot result_snapshot() const;

    // ---- Network play (BLE or LoRa; connection-type select first) ----
    /// Menu "Network Play" -> connection-type select screen (NetTransport).
    void open_network_menu();

    /// True when this build can actually run the BLE transport
    /// (BATTLESHIP_BLE=1, i.e. a Linux target with BlueZ/sd-bus). The macOS
    /// desktop build returns false and the NetTransport screen renders the
    /// BLE row disabled with a "(device only)" suffix.
    static constexpr bool ble_available() {
#if BATTLESHIP_BLE
        return true;
#else
        return false;
#endif
    }

    /// Persisted connection-type preference (BLE by default; survives
    /// restarts via settings.conf, PLAN.md Phase 6 "選択は設定に永続化").
    i18n::NetTransportPref net_transport_pref() const { return i18n::net_transport_pref(); }
    /// NetTransport screen selection: persists `pref` as the new default and
    /// advances to the Host/Join submenu. LoRa is the exception while the
    /// antenna has never been confirmed: nothing is persisted yet and the
    /// one-time antenna confirmation page (NetAntennaConfirm) is shown
    /// first (see confirm_lora_antenna()).
    void select_net_transport(i18n::NetTransportPref pref);
    /// NetAntennaConfirm "Enter: Yes": persists the antenna confirmation and
    /// the LoRa preference, then advances to the Host/Join submenu. (Esc on
    /// that page returns to NetTransport without persisting anything.)
    void confirm_lora_antenna();
    /// True when the network flow being set up uses LoRa (persisted
    /// preference). Unlike BLE this is available on every build: devices
    /// drive the Cap LoRa-1262, the desktop build a UDP radio simulator
    /// (net/lora/radio_factory.h, default_radio_backend()).
    bool net_using_lora() const {
        return i18n::net_transport_pref() == i18n::NetTransportPref::Lora;
    }
    /// True when the network flow being set up uses BLE: the persisted
    /// preference is BLE AND this build supports it. When BLE is preferred
    /// but unavailable (desktop build), host/join show the NetBleUnavailable
    /// error (the preference itself is NOT rewritten -- see
    /// i18n::NetTransportPref).
    bool net_using_ble() const {
        return ble_available() && i18n::net_transport_pref() == i18n::NetTransportPref::Ble;
    }
    /// NetworkMenu "Host": start advertising over the selected transport
    /// (BLE or LoRa) and show the lobby screen. BLE preferred but not
    /// available in this build -> NetError with NetBleUnavailable.
    void start_network_host();
    /// NetworkMenu "Join": start scanning over the selected transport and
    /// show the host-list screen (same BLE-unavailable handling as host).
    void start_network_join();
    /// NetJoin list entry activated: connect to peer `index` (see
    /// net_peer_count()).
    void join_host_at(size_t index);
    /// Cancel from lobby/join screens: tear down all network state, back to
    /// the Network menu.
    void cancel_network();

    /// Non-null while any network flow (hosting/joining/playing) is active.
    const battleship::net::NetSession* net_session() const { return net_session_.get(); }
    /// True when this peer opened the connection as the host (listener).
    bool net_is_host() const { return net_is_host_; }
    /// Transport-agnostic peer list for the NetJoin screen: BLE -> the BLE scan
    /// results, LoRa -> the link's beacon peers. Both change under
    /// net_hosts_dirty_subject(); join_host_at(index) accepts the same index
    /// space.
    size_t net_peer_count() const;
    /// Display label for peer `index` ("name (address)" on BLE, "name  -42 dBm" on LoRa). Empty if out of
    /// range.
    std::string net_peer_label(size_t index) const;
    /// Lobby/status line ("Waiting for opponent...", "Connection lost", ...).
    lv_subject_t* net_status_subject();
    /// Bumped whenever the discovered-host list changes (NetJoin rebuilds
    /// its buttons on this).
    lv_subject_t* net_hosts_dirty_subject();

    // ---- Boards the active screens should render ----
    // These dispatch on match_mode(): Game boards for hotseat/VsAi, the
    // NetSession's PlayerView boards for network play. Screens must use
    // these instead of reaching into game() directly so they work in every
    // mode (Phase 4).
    const battleship::core::Board& placement_board() const;
    const battleship::core::Board& battle_own_board() const;
    const battleship::core::TrackingBoard& battle_tracking_board() const;

private:
    void publish_all();
    void bump_board_dirty();
    void reset_placement_cursor();

    /// Schedules the AI's shot `delay_ms` from now via a one-shot LVGL timer
    /// (so the human sees their own shot result before the CPU replies).
    void schedule_ai_turn(uint32_t delay_ms);
    /// Cancels a pending AI-turn timer, if any (match reset / destruction).
    void cancel_ai_turn();
    /// Executes the AI's shot: fires, updates the status line with the CPU's
    /// target + outcome, and transitions to Result on a game-ending shot.
    void play_ai_turn();
    static void ai_timer_cb(lv_timer_t* timer);

    /// Fires a commentary request for `event` at the current game state
    /// (VsAi mode only -- hotseat never requests commentary). No-op if
    /// commentary is disabled (commentator_ is null).
    void request_commentary(battleship::commentary::CommentEvent event,
                            const battleship::commentary::CommentContext& context = {});
    // ---- Network internals (Phase 4) ----
    battleship::core::Board& active_placement_board();
    /// LVGL timer driving all net polling (100ms; 10ms for LoRa, where the
    /// UI-side work below is still throttled to 100ms) (session, BLE
    /// scanner, LoRa link) plus reactive mirroring of session state into subjects /
    /// page navigation. Never blocks: every net/ API is non-blocking.
    static void net_poll_timer_cb(lv_timer_t* timer);
    void net_poll();
    /// Creates the net poll timer if absent: 10 ms for LoRa flows, else 100 ms.
    void ensure_net_poll_timer();
    /// Applies a newly observed session state change (shot results, phase
    /// changes) to the status line / board-dirty subject / page navigation.
    void net_sync_session_state();
    void net_update_battle_status();
    /// Tears down session/transport/scanner/timer. Safe to call
    /// redundantly. A LoRa link that was Connected is closed (BYE attempted)
    /// and handed to the short-lived "closing" holder below instead of being
    /// destroyed on the spot, so the BYE can still go out.
    void teardown_network();

    // ---- LoRa internals (Phase 8) ----
    /// Opens the radio (finishing any still-draining closing holder first,
    /// so two radios are never open at once) and creates the link. On
    /// failure releases everything, routes to NetError with the mapped
    /// message and returns false.
    bool net_lora_open();
    /// Shows NetError for a radio start failure / link failure.
    void net_lora_show_error(i18n::StrId message);
    /// Closes the current LoRa link; keeps the closed objects alive in
    /// net_lora_closing_ for <= kLoraCloseDrainMs when a BYE may still be
    /// pending, otherwise destroys them immediately (radio before power).
    void net_lora_begin_close();
    /// Destroys the closing holder (if any) right now.
    void net_lora_finish_closing();
    static void net_lora_closing_timer_cb(lv_timer_t* timer);
    /// Monotonic millisecond clock for the LoRa link/transport.
    static uint64_t net_lora_now_ms();
    /// Test-only autopilot (BATTLESHIP_NET_AUTOPILOT=win|lose, desktop
    /// 2-process verification): auto-places the fleet and auto-fires on our
    /// turn so a full networked match can run unattended. See net_poll().
    void net_autopilot_step();

    /// LVGL repeating timer callback (see commentary_poll_timer_): checks
    /// whether the commentator's background thread has stashed new text in
    /// pending_commentary_ and, if so, publishes it to
    /// commentary_text_subject_ on the LVGL thread. This indirection is
    /// required because ICommentator callbacks may run on a worker thread
    /// (LlmCommentator) and must never touch LVGL objects/subjects directly
    /// (NOTES.md §8.5).
    static void commentary_poll_timer_cb(lv_timer_t* timer);

    model::BaseModel model_;
    battleship::core::Game game_;
    std::mt19937 rng_{std::random_device{}()};

    MatchMode match_mode_ = MatchMode::Hotseat;
    battleship::core::ai::Difficulty ai_difficulty_ = battleship::core::ai::Difficulty::Normal;
    std::unique_ptr<battleship::core::ai::AiPlayer> ai_;
    lv_timer_t* ai_timer_ = nullptr;
    /// Human's most recent shot result text, kept so the status line can show
    /// "You: Hit! | CPU B4: Miss" after the AI replies.
    std::string last_human_result_text_;

    battleship::core::PlayerId placement_player_ = battleship::core::PlayerId::One;
    battleship::core::Coord placement_cursor_{0, 0};
    battleship::core::Orientation placement_orientation_ = battleship::core::Orientation::Horizontal;

    battleship::core::Coord battle_cursor_{0, 0};
    LastShotInfo last_shot_{};

    // ---- Network play (BLE) ----
    std::unique_ptr<battleship::net::NetSession> net_session_;
#if BATTLESHIP_BLE
    std::unique_ptr<battleship::net::BleTransport> net_ble_transport_;
    battleship::net::BleScanner net_ble_scanner_;
    bool net_ble_scanning_ = false;
#endif
    // ---- LoRa (Phase 8) ----
    // Declaration order matters for destruction: radio handle (radio, then
    // Cap power) must outlive the link, which must outlive the transport;
    // teardown_network() also resets them explicitly in that order after
    // the NetSession that references the transport.
    battleship::net::lora::RadioHandle net_lora_radio_;
    std::unique_ptr<battleship::net::lora::LoRaLink> net_lora_link_;
    std::unique_ptr<battleship::net::lora::LoRaTransport> net_lora_transport_;
    /// True while the CURRENT flow runs over LoRa (see net_flow_ble_).
    bool net_flow_lora_ = false;
    /// Link state seen at the previous poll: distinguishes "join attempts
    /// exhausted" from a radio failure when the link reports Failed.
    battleship::net::lora::LinkState net_lora_seen_state_ = battleship::net::lora::LinkState::Idle;
    /// Closed-but-draining LoRa objects (BYE window) after teardown.
    struct LoraClosing;
    std::unique_ptr<LoraClosing> net_lora_closing_;
    lv_timer_t* net_lora_closing_timer_ = nullptr;

    /// True while the CURRENT hosting/joining/playing flow runs over BLE
    /// (as opposed to the persisted preference, which the user can change
    /// between matches without touching an in-flight session).
    bool net_flow_ble_ = false;
    lv_timer_t* net_poll_timer_ = nullptr;
    bool net_is_host_ = false;
    /// 0 = off, 1 = "win" (sweep the block the scripted fleet occupies),
    /// 2 = "lose" (fire only at known-water rows). From the
    /// BATTLESHIP_NET_AUTOPILOT env var; test tooling only.
    int net_autopilot_mode_ = 0;
    int net_autopilot_placed_ = 0;
    int net_autopilot_tick_ = 0;
    uint32_t net_poll_ui_last_ms_ = 0;  // LoRa: last 100 ms-rate net_poll pass
    uint32_t net_seen_change_counter_ = 0;
    battleship::net::SessionPhase net_seen_phase_ = battleship::net::SessionPhase::Handshake;
    size_t net_hosts_signature_ = 0;
    std::string net_own_result_text_;
    std::string net_incoming_result_text_;

    // ---- Commentary (Phase 3b) ----
    // Constructed once at startup via commentary::make_default_commentator();
    // nullptr means commentary is disabled entirely (BATTLESHIP_COMMENTARY=0)
    // and every request_commentary() call below becomes a no-op.
    std::unique_ptr<battleship::commentary::ICommentator> commentator_;
    // Mailbox the commentator's callback (possibly a worker thread, see
    // LlmCommentator) writes into; commentary_poll_timer_ drains it on the
    // LVGL thread. Guarded by commentary_mutex_ since TemplateCommentator's
    // callback fires synchronously on whatever thread calls
    // request_comment() (here, the LVGL thread) while LlmCommentator's fires
    // from its worker thread -- both must be safe.
    std::mutex commentary_mutex_;
    std::string pending_commentary_;
    bool commentary_pending_ = false;
    lv_timer_t* commentary_poll_timer_ = nullptr;

    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject board_dirty_subject_;
    reactive::StringSubject<64> status_text_subject_;
    reactive::StringSubject<160> commentary_text_subject_;
    // 192 bytes: the longest message (JA NetLoraCapMissing, ~110 bytes of
    // UTF-8) must fit without being cut mid-character.
    reactive::StringSubject<192> net_status_subject_;
    reactive::IntSubject net_hosts_dirty_subject_;
    reactive::IntSubject language_version_subject_;
    reactive::BoolSubject interrupt_modal_subject_;
    reactive::BoolSubject title_quit_armed_subject_;
    bool interrupt_modal_visible_ = false;
};

} // namespace viewmodel
