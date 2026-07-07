/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "commentary/llm_commentator.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "commentary/snapshot_summary.h"
#include "llama.h"
#include "logger.h"

namespace battleship::commentary {

using battleship::core::MatchSnapshot;

LlmCommentator::LlmCommentator(std::string model_path, LlmConfig config, std::uint64_t fallback_seed)
    : config_(config), fallback_(fallback_seed) {
    if (model_path.empty()) {
        LOG_INFO("LlmCommentator: no model path configured, using template fallback only");
    } else {
        llama_backend_init();

        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;  // CPU-only by design (see CMakeLists.txt BATTLESHIP_LLM notes)

        model_ = llama_model_load_from_file(model_path.c_str(), model_params);
        if (!model_) {
            LOG_ERROR("LlmCommentator: failed to load model '{}', falling back to templates",
                     model_path);
        } else {
            vocab_ = llama_model_get_vocab(model_);
            LOG_INFO("LlmCommentator: loaded model '{}'", model_path);
        }
    }

    worker_ = std::thread(&LlmCommentator::worker_loop, this);
}

LlmCommentator::~LlmCommentator() {
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        next_request_.reset();
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
}

void LlmCommentator::request_comment(const MatchSnapshot& snapshot, CommentEvent event,
                                     const CommentContext& context) {
    const std::uint64_t id = ++latest_request_id_;

    if (!model_ || lang_ != Lang::EN) {
        // No model loaded, OR a non-English output language: deliver the
        // template fallback synchronously, same as TemplateCommentator does
        // on its own -- still cheap string formatting, no thread hop needed.
        //
        // The lang_ != EN branch is a Phase 5 quality decision, not a
        // capability gap in the plumbing: SmolLM2-360M-Instruct simply
        // cannot produce Japanese/Chinese. Measured with the real model
        // (see NOTES.md Phase 5): with an explicit target-language
        // instruction (phrased in the target language itself), all 10/10
        // JA+ZH probe generations still came back in English -- the model's
        // training corpus is English-dominant and 360M params leaves no
        // multilingual headroom. Rather than show English "commentary" under
        // a JA/ZH UI, JA/ZH use the localized TemplateCommentator banks; EN
        // keeps the full LLM experience. Revisit if the model is ever
        // swapped for a multilingual one (e.g. Qwen2.5-0.5B-Instruct).
        fallback_.set_callback(callback_);
        fallback_.request_comment(snapshot, event, context);
        return;
    }

    PendingRequest request;
    request.id = id;
    request.prompt = build_llm_prompt(snapshot, event, context, lang_);
    request.event = event;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        next_request_ = std::move(request);
    }
    cv_.notify_one();
}

void LlmCommentator::cancel_pending() {
    // Bump the abandonment watermark to the latest id issued so far; any
    // result the worker produces for an id <= this value is dropped instead
    // of delivered (see worker_loop()). Also clear anything merely queued
    // and not yet picked up.
    abandoned_up_to_id_.store(latest_request_id_.load());
    std::lock_guard<std::mutex> lock(mutex_);
    next_request_.reset();
}

void LlmCommentator::set_callback(CommentCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = std::move(callback);
}

void LlmCommentator::set_language(Lang lang) {
    lang_ = lang;
    fallback_.set_language(lang);
}

Lang LlmCommentator::language() const {
    return lang_;
}

void LlmCommentator::worker_loop() {
    while (!stop_.load()) {
        PendingRequest request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stop_.load() || next_request_.has_value(); });
            if (stop_.load()) {
                return;
            }
            request = std::move(*next_request_);
            next_request_.reset();
        }

        if (request.id <= abandoned_up_to_id_.load()) {
            continue;  // superseded/cancelled before we even started
        }

        const auto start = std::chrono::steady_clock::now();
        std::string text = run_inference(request.prompt);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        last_generation_ms_.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

        if (request.id <= abandoned_up_to_id_.load()) {
            continue;  // cancelled while generating; drop the result
        }
        // A newer request superseding this one (without an explicit
        // cancel_pending()) is fine to still deliver -- request_comment()'s
        // contract is "only the latest *matters*", and by the time
        // generation finishes there may already be a fresher one queued;
        // that fresher one will simply overwrite this text shortly after
        // via its own callback invocation, which is the desired behavior.

        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callback_ && !text.empty()) {
            callback_(text);
        }
    }
}

std::string LlmCommentator::run_inference(const std::string& prompt) {
    if (!model_ || !vocab_) {
        return {};
    }

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.context_size;
    ctx_params.n_batch = config_.context_size;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_threads_batch = config_.n_threads;

    // Fresh context per request (NOTES.md Phase 3b: "コンテキストは毎回リセット
    // でよい（KVキャッシュ節約、512MB 実機を意識）") -- trades a bit of
    // per-request setup latency for bounded, predictable memory use instead
    // of an ever-growing KV cache across a whole match.
    llama_context* ctx = llama_init_from_model(model_, ctx_params);
    if (!ctx) {
        LOG_ERROR("LlmCommentator: failed to create llama context");
        return {};
    }

    // Tokenize prompt.
    const int n_prompt_tokens_max = static_cast<int>(prompt.size()) + 8;
    std::vector<llama_token> tokens(n_prompt_tokens_max);
    const int n_tokens = llama_tokenize(vocab_, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                        tokens.data(), n_prompt_tokens_max, /*add_special=*/true,
                                        /*parse_special=*/true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        llama_tokenize(vocab_, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(),
                      -n_tokens, true, true);
    } else {
        tokens.resize(n_tokens);
    }

    if (static_cast<int>(tokens.size()) >= config_.context_size) {
        LOG_ERROR("LlmCommentator: prompt too long for context ({} tokens)", tokens.size());
        llama_free(ctx);
        return {};
    }

    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
    if (llama_decode(ctx, batch) != 0) {
        LOG_ERROR("LlmCommentator: initial decode failed");
        llama_free(ctx);
        return {};
    }

    // Repetition penalty (last 64 tokens) discourages the small model from
    // looping on phrases like "Your response:" repeatedly; penalty_last_n=0
    // would disable it entirely, so keep a modest window instead. Chained
    // before temp/dist so it reshapes logits before sampling, matching
    // llama.cpp's own recommended chain ordering.
    llama_sampler* sampler_chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler_chain,
                            llama_sampler_init_penalties(/*penalty_last_n=*/64,
                                                         /*penalty_repeat=*/1.3F,
                                                         /*penalty_freq=*/0.0F,
                                                         /*penalty_present=*/0.0F));
    llama_sampler_chain_add(sampler_chain, llama_sampler_init_temp(config_.temperature));
    llama_sampler_chain_add(sampler_chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string output;
    output.reserve(static_cast<std::size_t>(config_.max_tokens) * 4);

    llama_token new_token = 0;
    for (int i = 0; i < config_.max_tokens; ++i) {
        new_token = llama_sampler_sample(sampler_chain, ctx, -1);
        if (llama_vocab_is_eog(vocab_, new_token)) {
            break;
        }

        char piece_buf[256];
        const int piece_len =
            llama_token_to_piece(vocab_, new_token, piece_buf, sizeof(piece_buf), 0, true);
        if (piece_len > 0) {
            output.append(piece_buf, static_cast<std::size_t>(piece_len));
        }

        // Stop at the first blank line: the small model tends to answer in
        // one short paragraph and then either echo the prompt format or
        // start a new "Your response:"-style turn -- cutting at the first
        // double-newline reliably keeps just the initial reaction sentence
        // without needing a much larger/slower model.
        if (output.find("\n\n") != std::string::npos) {
            break;
        }

        llama_sampler_accept(sampler_chain, new_token);
        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx, next_batch) != 0) {
            break;
        }
    }

    llama_sampler_free(sampler_chain);
    llama_free(ctx);

    // Trim leading/trailing whitespace, and drop anything from the first
    // blank line onward (see the stopping condition above -- the loop can
    // still exit on max_tokens/EOG mid-way through appending the second
    // newline run before the check fires on the *next* token).
    auto cut_at = output.find("\n\n");
    std::string trimmed = cut_at == std::string::npos ? output : output.substr(0, cut_at);

    const auto first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = trimmed.find_last_not_of(" \t\r\n");
    return trimmed.substr(first, last - first + 1);
}

}  // namespace battleship::commentary
