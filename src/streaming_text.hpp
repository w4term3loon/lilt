// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace ren::detail {

// Inputs are normalized UTF-8 hypotheses. A space-terminated common prefix
// contains complete words and always ends at a UTF-8 byte boundary. The last
// word stays tentative, even for identical hypotheses. Later revisions may
// shorten this prefix; agreement is a display hint, not a correctness promise.
inline std::size_t stable_word_prefix(std::string_view previous, std::string_view current) {
    std::size_t common = 0;
    while (common < previous.size() && common < current.size() && previous[common] == current[common])
        ++common;
    while (common && current[common - 1] != ' ') --common;
    return common;
}

// One pending value: a producer replaces stale snapshots while the consumer
// works. Closing drops pending work and wakes a waiting consumer.
template<class Value>
class LatestValue {
public:
    void publish(Value value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            pending_ = std::move(value);
        }
        ready_.notify_one();
    }

    std::optional<Value> take(std::chrono::steady_clock::time_point earliest =
                                 std::chrono::steady_clock::time_point::min()) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (std::chrono::steady_clock::now() < earliest)
            ready_.wait_until(lock, earliest, [&] { return closed_; });
        ready_.wait(lock, [&] { return closed_ || pending_.has_value(); });
        if (closed_) return std::nullopt;
        auto result = std::move(pending_);
        pending_.reset();
        return result;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            pending_.reset();
        }
        ready_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<Value> pending_;
    bool closed_ = false;
};

} // namespace ren::detail
