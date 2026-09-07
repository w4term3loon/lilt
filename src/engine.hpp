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
// callback. Idle owns neither a microphone stream nor a decoder worker.
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
    // model loading runs alongside capture; the context is freed after each
    // session. Cancellation closes capture promptly, but joining a model load
    // can take longer because Whisper initialization has no abort hook.
    // Optional previews re-decode growing audio snapshots at most every two
    // seconds, coalescing pending work. This is not a native streaming model.
    bool start(const std::string& model_path, const std::string& language,
               int threads, Callbacks callbacks, bool live_preview = false);
    void stop();
    void cancel();
    bool busy() const noexcept;

    // Offline entry points: mono, 16 kHz float samples, or a 16 kHz mono WAV
    // (PCM16 / IEEE float32). Audio is limited to 180 seconds. Errors throw
    // std::runtime_error; silence returns an empty string without loading a model.
    static std::string transcribe(const std::string& model_path,
                                  const std::vector<float>& samples,
                                  const std::string& language = "auto", int threads = 0);
    static std::string transcribe_file(const std::string& model_path,
                                       const std::string& wav_path,
                                       const std::string& language = "auto", int threads = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lilt
