// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace lilt {

// State, level, and final-result callbacks run on the capture worker; partial
// callbacks run on a separate decoder worker and may run concurrently with
// them. Marshal GUI changes to the main loop. Do not destroy the engine from a
// callback. Idle owns no microphone, recording, transcript, or decoder state;
// only the selected model's weights remain warm for up to 60 seconds.
class Engine {
public:
    struct Callbacks {
        std::function<void(std::string, std::string)> on_state;
        std::function<void(double)> on_level;
        std::function<void(std::string)> on_result;
        // UTF-8 text plus the byte length of a complete-word prefix shared by
        // the previous hypothesis. It can shrink after a correction and is
        // provisional; only on_result supplies text for final insertion.
        std::function<void(std::string, std::size_t)> on_partial;
    };

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Returns false if a session is already active. Once the microphone is ready,
    // model loading runs alongside capture. Every session has fresh decoder
    // state. Cancellation closes capture promptly, but joining model/state
    // initialization can take longer because Whisper has no abort hook there.
    // Optional previews re-decode growing audio snapshots at most every two
    // seconds, coalescing pending work. This is not a native streaming model.
    bool start(const std::string& model_path, int threads, Callbacks callbacks,
               bool live_preview = false);
    void stop();
    void cancel();
    bool busy() const noexcept;
    // Drop cached weights on model selection changes. An active operation keeps
    // its own weights until it ends; clearing does not cancel that operation.
    void release_model();

    // Offline entry points: mono, 16 kHz float samples, or a 16 kHz mono WAV
    // (PCM16 / IEEE float32). Audio is limited to 180 seconds. Errors throw
    // std::runtime_error; silence returns an empty string without loading a model.
    // These synchronous English-only calls share the recording weight cache,
    // replace it for another model, reject overlap, and obey cancel(). Each call
    // discards its decoder state. Destruction/release_model() drop idle weights.
    std::string transcribe(const std::string& model_path, const std::vector<float>& samples,
                           int threads = 0);
    std::string transcribe_file(const std::string& model_path, const std::string& wav_path,
                                int threads = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lilt
