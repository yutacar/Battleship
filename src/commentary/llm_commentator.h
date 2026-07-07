/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * LlmCommentator: llama.cpp-backed ICommentator. Only compiled when
 * BATTLESHIP_LLM=ON (see CMakeLists.txt); callers that want a single code
 * path regardless of build configuration should use
 * make_default_commentator() in commentator_factory.h instead of
 * constructing this directly.
 *
 * Threading model (NOTES.md §8.5 / PLAN.md §2.4 "完全非同期"):
 *   - Construction loads the GGUF model file and creates the llama.cpp
 *     context ONCE, synchronously, on whatever thread constructs this object
 *     (expected: app startup, not the per-turn hot path).
 *   - request_comment() only builds a prompt string and hands it to a single
 *     background worker thread via a mutex + condition_variable queue-of-one
 *     (latest request wins -- an in-flight generation is not interrupted
 *     mid-decode, but a newer request queued while busy replaces any request
 *     that was merely *waiting*, and a completed stale result is dropped
 *     rather than delivered). It never blocks the calling (LVGL) thread on
 *     inference.
 *   - The worker thread calls the CommentCallback directly upon completion;
 *     because that callback may touch LVGL objects, callers MUST NOT touch
 *     LVGL from within the callback directly -- BaseViewModel instead has
 *     the callback stash the text behind a mutex and relies on an LVGL timer
 *     to poll and publish it to a subject on the LVGL thread (see
 *     base_viewmodel.cpp's commentary_poll_timer_).
 *   - cancel_pending() marks the current generation/queued request as
 *     "abandoned"; if a result arrives for an abandoned request it is
 *     dropped silently (no callback invoked).
 *
 * Fallback: if the model file cannot be loaded (missing path, bad file,
 * llama.cpp init failure), the constructor leaves the object in a
 * "unavailable" state and every request_comment() call synchronously
 * delegates to an internal TemplateCommentator instead -- callers do not
 * need to check availability themselves. See is_model_loaded().
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "commentary/commentator.h"
#include "commentary/template_commentator.h"

struct llama_model;
struct llama_context;
struct llama_vocab;
struct llama_sampler;

namespace battleship::commentary {

/// Tuning knobs kept small and explicit rather than a config struct grab-bag
/// so call sites read clearly; see NOTES.md Phase 3b for the rationale
/// behind the defaults (short generations, low temperature, fresh context
/// per request to bound RAM use on a 512MB target).
struct LlmConfig {
    int max_tokens = 48;
    float temperature = 0.4F;
    int context_size = 512;
    int n_threads = 2;
};

class LlmCommentator final : public ICommentator {
public:
    /// `model_path` empty or unloadable => falls back to TemplateCommentator
    /// for every request (see class doc). `fallback_seed` seeds that
    /// internal fallback instance.
    explicit LlmCommentator(std::string model_path, LlmConfig config = {},
                            std::uint64_t fallback_seed = std::random_device{}());
    ~LlmCommentator() override;

    LlmCommentator(const LlmCommentator&) = delete;
    LlmCommentator& operator=(const LlmCommentator&) = delete;

    void request_comment(const battleship::core::MatchSnapshot& snapshot, CommentEvent event,
                         const CommentContext& context = {}) override;
    void cancel_pending() override;
    void set_callback(CommentCallback callback) override;
    void set_language(Lang lang) override;
    Lang language() const override;

    /// True if the GGUF model loaded successfully and the worker thread is
    /// running LLM inference; false means every request silently falls back
    /// to canned template text (still delivered via the same callback).
    bool is_model_loaded() const { return model_ != nullptr; }

    /// Measured wall-clock latency (milliseconds) of the most recently
    /// completed LLM generation, or -1 if none has completed yet. Exposed
    /// for the Phase 3b latency measurement requested in NOTES.md; not used
    /// by production UI code.
    long last_generation_ms() const { return last_generation_ms_.load(); }

private:
    struct PendingRequest {
        std::uint64_t id = 0;
        std::string prompt;
        CommentEvent event = CommentEvent::MatchStart;
    };

    void worker_loop();
    std::string run_inference(const std::string& prompt);

    LlmConfig config_;
    Lang lang_ = Lang::EN;
    CommentCallback callback_;
    std::mutex callback_mutex_;  ///< guards callback_ (settable from any thread)

    // llama.cpp handles. Kept as raw pointers with explicit free() in the
    // destructor (matches llama.cpp's C API, which predates smart-pointer
    // idioms in its own headers) rather than wrapping in unique_ptr with a
    // custom deleter, to keep the .h free of llama.cpp-specific includes.
    llama_model* model_ = nullptr;
    const llama_vocab* vocab_ = nullptr;

    TemplateCommentator fallback_;

    // Worker thread + single-slot "latest request" mailbox.
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<PendingRequest> next_request_;  ///< guarded by mutex_
    std::atomic<std::uint64_t> latest_request_id_{0};
    std::atomic<std::uint64_t> abandoned_up_to_id_{0};
    std::atomic<bool> stop_{false};
    std::atomic<long> last_generation_ms_{-1};
};

}  // namespace battleship::commentary
