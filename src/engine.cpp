// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.hpp"
#include "text.hpp"
#include "streaming_text.hpp"

#include <pulse/pulseaudio.h>
#include <whisper.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace lilt {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t sample_rate = 16000;
constexpr std::size_t max_samples = sample_rate * 180;

template<typename Callback, typename... Args>
void notify(const Callback& callback, Args&&... args) noexcept {
    if (callback) {
        // A GUI callback must not strand the worker or its microphone stream.
        try { callback(std::forward<Args>(args)...); } catch (...) {}
    }
}

bool interrupted(const std::atomic<bool>* cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

bool audible(const std::vector<float>& samples) {
    if (samples.size() > max_samples)
        throw std::runtime_error("Audio exceeds the 180-second recording limit.");
    // This is only a quiet-audio gate, not a voice classifier. Whisper's own
    // no-speech probability supplies the second check after decoding.
    constexpr std::size_t frame = 320; // 20 ms
    std::size_t active_frames = 0;
    double energy = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto value = samples[i];
        if (!std::isfinite(value))
            throw std::runtime_error("Audio contains invalid floating-point samples.");
        energy += static_cast<double>(value) * value;
        if ((i + 1) % frame == 0) {
            if (energy / frame >= 0.000001) ++active_frames;
            energy = 0;
        }
    }
    return samples.size() >= sample_rate / 10 && active_frames >= 3;
}

using WhisperContext = std::unique_ptr<whisper_context, decltype(&whisper_free)>;

WhisperContext load_model(const std::string& model_path, const std::string& language) {
    if (!language.empty() && language != "auto" && whisper_lang_id(language.c_str()) < 0)
        throw std::runtime_error("Unknown transcription language: " + language);
    if (!std::ifstream(model_path, std::ios::binary))
        throw std::runtime_error("Cannot open the model. Choose a downloaded whisper.cpp model in Settings.");

    auto context_params = whisper_context_default_params();
    context_params.use_gpu = false;
    WhisperContext context(
        whisper_init_from_file_with_params(model_path.c_str(), context_params), whisper_free);
    if (!context) throw std::runtime_error("Cannot load the model. Check that the download is complete.");

    const bool multilingual = whisper_is_multilingual(context.get());
    if (!multilingual && !language.empty() && language != "auto" && language != "en" && language != "english")
        throw std::runtime_error("This model supports English only. Select a multilingual model for other languages.");
    return context;
}

std::string decode_samples(whisper_context* context, const std::vector<float>& samples,
                            const std::string& language, int threads, std::atomic<bool>* cancel) {
    if (interrupted(cancel)) return {};
    const bool multilingual = whisper_is_multilingual(context);

    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    const int available = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    params.n_threads = threads > 0 ? std::clamp(threads, 1, available) : std::clamp(available / 2, 1, 8);
    params.language = multilingual ? (language.empty() ? "auto" : language.c_str()) : "en";
    params.translate = false;
    params.no_context = true;
    params.no_timestamps = true;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.suppress_blank = true;
    params.suppress_nst = true;
    params.greedy.best_of = 1;
    params.abort_callback = [](void* data) { return interrupted(static_cast<std::atomic<bool>*>(data)); };
    params.abort_callback_user_data = cancel;
    params.encoder_begin_callback = [](whisper_context*, whisper_state*, void* data) {
        return !interrupted(static_cast<std::atomic<bool>*>(data));
    };
    params.encoder_begin_callback_user_data = cancel;

    // Whisper rejects extremely short inputs; pad a short utterance with silence.
    // Longer dictation goes straight to inference without duplicating the buffer.
    std::vector<float> padded;
    const float* audio = samples.data();
    std::size_t count = samples.size();
    if (count < sample_rate) {
        padded = samples;
        padded.resize(sample_rate, 0.0f);
        audio = padded.data();
        count = padded.size();
    }
    const int result = whisper_full(context, params, audio, static_cast<int>(count));
    if (interrupted(cancel)) return {};
    if (result != 0) throw std::runtime_error("Local transcription failed. Try a shorter recording or another model.");

    std::string text;
    for (int i = 0; i < whisper_full_n_segments(context); ++i) {
        if (whisper_full_get_segment_no_speech_prob(context, i) > 0.6f) continue;
        if (const char* segment = whisper_full_get_segment_text(context, i)) text += segment;
    }
    return normalize_dictation(text);
}

std::string transcribe_samples(const std::string& model_path,
                               const std::vector<float>& samples,
                               const std::string& language, int threads,
                               std::atomic<bool>* cancel) {
    if (interrupted(cancel) || !audible(samples)) return {};
    auto context = load_model(model_path, language);
    return decode_samples(context.get(), samples, language, threads, cancel);
}

class SessionDecoder {
public:
    SessionDecoder(std::string model_path, std::string language, int threads,
                   std::atomic<bool>& cancel, const Engine::Callbacks& callbacks, bool live_preview)
        : model_path_(std::move(model_path)), language_(std::move(language)), threads_(threads),
          cancel_(cancel), on_partial_(callbacks.on_partial), preview_(live_preview && bool(on_partial_)) {}

    ~SessionDecoder() {
        finish();
        // Joining precedes destruction of the queue, callback, and cancellation
        // references. Model initialization has no abort hook; partial inference
        // uses its own abort flag and finishes before final inference begins.
        if (task_.valid()) task_.wait();
    }

    void start() {
        task_ = std::async(std::launch::async, [this] {
            auto context = load_model(model_path_, language_);
            if (preview_) {
                std::string previous;
                auto next_decode = Clock::now();
                while (auto samples = pending_.take(next_decode)) {
                    if (partial_stop_ || cancel_) break;
                    if (!audible(*samples)) continue;
                    next_decode = Clock::now() + 2s;
                    auto text = decode_samples(context.get(), *samples, language_, threads_, &partial_stop_);
                    if (partial_stop_ || cancel_) break;
                    const auto stable = detail::stable_word_prefix(previous, text);
                    previous = text;
                    notify(on_partial_, std::move(text), stable);
                }
            }
            return context;
        });
    }

    void submit(const std::vector<float>& samples) {
        if (preview_ && !partial_stop_ && !cancel_) pending_.publish(samples);
    }

    void poll() {
        if (task_.valid() && task_.wait_for(0ms) == std::future_status::ready)
            context_ = task_.get(); // Surface model/partial-decoder errors during capture.
    }

    void finish() {
        partial_stop_ = true;
        pending_.close();
    }

    WhisperContext take_context() {
        finish();
        if (task_.valid()) context_ = task_.get();
        return std::move(context_);
    }

private:
    const std::string model_path_, language_;
    const int threads_;
    std::atomic<bool>& cancel_;
    const std::function<void(std::string, std::size_t)> on_partial_;
    const bool preview_;
    std::atomic<bool> partial_stop_{false};
    detail::LatestValue<std::vector<float>> pending_;
    WhisperContext context_{nullptr, whisper_free};
    std::future<WhisperContext> task_;
};

struct PulseCapture {
    pa_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    pa_stream* stream = nullptr;

    ~PulseCapture() {
        if (stream) {
            pa_stream_disconnect(stream);
            pa_stream_unref(stream);
        }
        if (context) {
            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        if (loop) pa_mainloop_free(loop);
    }

    std::runtime_error error(const std::string& message) const {
        return std::runtime_error(message + (context ? std::string(": ") + pa_strerror(pa_context_errno(context)) : ""));
    }

    void iterate() {
        if (pa_mainloop_iterate(loop, 0, nullptr) < 0)
            throw error("Audio server stopped responding");
    }
};

std::vector<float> capture_audio(std::atomic<bool>& stop, std::atomic<bool>& cancel,
                                 const Engine::Callbacks& callbacks,
                                 SessionDecoder& decoder) {
    PulseCapture pulse;
    pulse.loop = pa_mainloop_new();
    if (!pulse.loop) throw std::runtime_error("Cannot create the audio connection.");
    pulse.context = pa_context_new(pa_mainloop_get_api(pulse.loop), "lilt");
    if (!pulse.context || pa_context_connect(pulse.context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
        throw pulse.error("Cannot connect to Ubuntu's audio server");

    auto deadline = Clock::now() + 8s;
    while (pa_context_get_state(pulse.context) != PA_CONTEXT_READY) {
        if (stop || cancel) return {};
        if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(pulse.context)))
            throw pulse.error("Cannot connect to Ubuntu's audio server");
        if (Clock::now() >= deadline) throw std::runtime_error("Timed out connecting to Ubuntu's audio server.");
        pulse.iterate();
        std::this_thread::sleep_for(10ms);
    }

    const pa_sample_spec spec{PA_SAMPLE_FLOAT32NE, sample_rate, 1};
    pulse.stream = pa_stream_new(pulse.context, "Dictation microphone", &spec, nullptr);
    if (!pulse.stream) throw pulse.error("Cannot create the microphone stream");
    pa_buffer_attr buffer{};
    buffer.maxlength = static_cast<std::uint32_t>(-1);
    buffer.tlength = static_cast<std::uint32_t>(-1);
    buffer.prebuf = static_cast<std::uint32_t>(-1);
    buffer.minreq = static_cast<std::uint32_t>(-1);
    buffer.fragsize = static_cast<std::uint32_t>(sample_rate * sizeof(float) / 20); // 50 ms
    if (pa_stream_connect_record(pulse.stream, nullptr, &buffer, PA_STREAM_ADJUST_LATENCY) < 0)
        throw pulse.error("Cannot open the default microphone");

    deadline = Clock::now() + 8s;
    while (pa_stream_get_state(pulse.stream) != PA_STREAM_READY) {
        if (stop || cancel) return {};
        if (!PA_STREAM_IS_GOOD(pa_stream_get_state(pulse.stream)))
            throw pulse.error("Cannot open the default microphone");
        if (Clock::now() >= deadline) throw std::runtime_error("Timed out opening the default microphone.");
        pulse.iterate();
        std::this_thread::sleep_for(10ms);
    }

    std::vector<float> samples;
    samples.reserve(sample_rate * 10);
    notify(callbacks.on_state, "recording", "Listening");
    // Start initialization only once the microphone is ready. It runs on a
    // separate thread, so reading audio never waits for the model to load.
    if (stop || cancel) return samples;
    decoder.start();
    const auto started = Clock::now();
    auto last_audio = started;
    auto last_level = started;
    auto last_preview = started;
    double level_energy = 0;
    std::size_t level_count = 0;
    while (!cancel) {
        decoder.poll(); // A failed load/partial decode closes the microphone promptly.
        pulse.iterate();
        if (pa_stream_get_state(pulse.stream) != PA_STREAM_READY)
            throw pulse.error("The microphone was disconnected");

        // All libpulse calls stay on this worker. Nonblocking iteration keeps
        // cancellation responsive even when the server supplies no audio.
        std::size_t readable = pa_stream_readable_size(pulse.stream);
        if (readable == static_cast<std::size_t>(-1)) throw pulse.error("Cannot read the microphone");
        while (readable > 0 && samples.size() < max_samples && !cancel) {
            const void* data = nullptr;
            std::size_t bytes = 0;
            if (pa_stream_peek(pulse.stream, &data, &bytes) < 0) throw pulse.error("Cannot read the microphone");
            if (!bytes) break;
            const auto count = std::min(bytes / sizeof(float), max_samples - samples.size());
            const auto* values = static_cast<const float*>(data);
            for (std::size_t i = 0; i < count; ++i) {
                const float value = values && std::isfinite(values[i]) ? std::clamp(values[i], -1.0f, 1.0f) : 0.0f;
                samples.push_back(value); // Null data with a nonzero size is a PulseAudio hole.
                level_energy += static_cast<double>(value) * value;
            }
            level_count += count;
            if (pa_stream_drop(pulse.stream) < 0) throw pulse.error("Cannot read the microphone");
            if (count) last_audio = Clock::now();
            readable = pa_stream_readable_size(pulse.stream);
            if (readable == static_cast<std::size_t>(-1)) throw pulse.error("Cannot read the microphone");
        }
        const auto now = Clock::now();
        if (now - last_level >= 100ms) {
            const double rms = level_count ? std::sqrt(level_energy / level_count) : 0;
            notify(callbacks.on_level, std::clamp(rms * 5.0, 0.0, 1.0));
            last_level = now;
            level_energy = 0;
            level_count = 0;
        }
        if (stop || samples.size() >= max_samples || now - started >= 180s) break;
        if (now - last_preview >= 2s) {
            decoder.submit(samples);
            last_preview = now;
        }
        if (now - last_audio >= 5s) throw std::runtime_error("The microphone stopped providing audio.");
        std::this_thread::sleep_for(10ms);
    }
    return samples; // Close the microphone before waiting for loading or decoding.
}

std::uint16_t little16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
}

std::uint32_t little32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::vector<float> read_wav(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open WAV file: " + path);
    const auto file_size = file.tellg();
    if (file_size < 12) throw std::runtime_error("Invalid or truncated WAV file.");
    file.seekg(0);
    std::array<unsigned char, 12> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    if (std::memcmp(header.data(), "RIFF", 4) || std::memcmp(header.data() + 8, "WAVE", 4))
        throw std::runtime_error("Expected a RIFF/WAVE audio file.");
    const std::uint64_t end = static_cast<std::uint64_t>(little32(header.data() + 4)) + 8;
    if (end > static_cast<std::uint64_t>(file_size) || end < 12)
        throw std::runtime_error("Invalid or truncated WAV file.");

    std::uint16_t format = 0, channels = 0, bits = 0, alignment = 0;
    std::uint32_t rate = 0;
    std::uint64_t data_offset = 0;
    std::uint32_t data_size = 0;
    bool have_format = false, have_data = false;
    for (std::uint64_t offset = 12; offset + 8 <= end;) {
        std::array<unsigned char, 8> chunk{};
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        const auto size = little32(chunk.data() + 4);
        const auto next = offset + 8 + size + (size & 1u);
        if (!file || next > end) throw std::runtime_error("Invalid or truncated WAV chunk.");
        if (!std::memcmp(chunk.data(), "fmt ", 4)) {
            if (have_format || size < 16) throw std::runtime_error("Invalid WAV format chunk.");
            std::array<unsigned char, 16> fmt{};
            file.read(reinterpret_cast<char*>(fmt.data()), fmt.size());
            format = little16(fmt.data());
            channels = little16(fmt.data() + 2);
            rate = little32(fmt.data() + 4);
            alignment = little16(fmt.data() + 12);
            bits = little16(fmt.data() + 14);
            have_format = true;
        } else if (!std::memcmp(chunk.data(), "data", 4)) {
            if (have_data) throw std::runtime_error("WAV files with multiple data chunks are not supported.");
            data_offset = offset + 8;
            data_size = size;
            have_data = true;
        }
        offset = next;
    }
    if (!have_format || !have_data) throw std::runtime_error("WAV file is missing its format or audio data.");
    if (channels != 1 || rate != sample_rate || !((format == 1 && bits == 16) || (format == 3 && bits == 32)))
        throw std::runtime_error("Use a 16 kHz mono WAV file containing PCM16 or float32 audio.");
    const auto width = bits / 8;
    if (alignment != width || data_size % width)
        throw std::runtime_error("Invalid WAV sample alignment.");
    const auto count = data_size / width;
    if (count > max_samples) throw std::runtime_error("Audio exceeds the 180-second recording limit.");
    file.seekg(static_cast<std::streamoff>(data_offset));
    std::vector<unsigned char> bytes(data_size);
    file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!file) throw std::runtime_error("Truncated WAV audio data.");
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (format == 1) {
            const auto value = little16(bytes.data() + i * width);
            samples[i] = (value < 0x8000 ? static_cast<int>(value) : static_cast<int>(value) - 0x10000) / 32768.0f;
        } else {
            const auto value = little32(bytes.data() + i * width);
            static_assert(sizeof(float) == sizeof(value), "IEEE float32 is required");
            std::memcpy(&samples[i], &value, sizeof(value));
        }
    }
    return samples;
}

} // namespace

struct Engine::Impl {
    std::mutex lifecycle;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> cancel_requested{false};

    ~Impl() {
        cancel_requested = true;
        stop_requested = true;
        if (worker.joinable()) worker.join();
    }

    void run(const std::string& model_path, const std::string& language,
             int threads, const Callbacks& callbacks, bool live_preview) noexcept {
        std::string terminal_state = "idle";
        std::string message;
        try {
            if (!std::ifstream(model_path, std::ios::binary))
                throw std::runtime_error("Choose a downloaded whisper.cpp model in Settings before recording.");
            SessionDecoder decoder(model_path, language, threads, cancel_requested, callbacks, live_preview);
            auto samples = capture_audio(stop_requested, cancel_requested, callbacks, decoder);
            decoder.finish();
            if (!cancel_requested && audible(samples)) {
                notify(callbacks.on_state, "transcribing", "Transcribing");
                auto context = decoder.take_context();
                if (!context) throw std::runtime_error("The transcription model was not initialized.");
                auto text = decode_samples(context.get(), samples, language, threads, &cancel_requested);
                if (!cancel_requested && !text.empty()) notify(callbacks.on_result, std::move(text));
                else if (!cancel_requested) message = "No speech detected";
            } else if (!cancel_requested) {
                // A real recording preloads even if it later proves silent.
                // Report a load failure, but keep silence out of inference.
                auto context = decoder.take_context();
                message = "No speech detected";
            }
            if (cancel_requested) message = "Cancelled";
        } catch (const std::exception& error) {
            terminal_state = cancel_requested ? "idle" : "error";
            message = cancel_requested ? "Cancelled" : error.what();
        } catch (...) {
            terminal_state = "error";
            message = "Unexpected audio or transcription error.";
        }
        notify(callbacks.on_level, 0.0);
        running = false;
        notify(callbacks.on_state, std::move(terminal_state), std::move(message));
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

bool Engine::start(const std::string& model_path, const std::string& language,
                   int threads, Callbacks callbacks, bool live_preview) {
    std::lock_guard<std::mutex> guard(impl_->lifecycle);
    if (impl_->running) return false;
    if (impl_->worker.joinable()) {
        if (impl_->worker.get_id() == std::this_thread::get_id()) return false;
        impl_->worker.join();
    }
    impl_->stop_requested = false;
    impl_->cancel_requested = false;
    impl_->running = true;
    try {
        impl_->worker = std::thread([this, model_path, language, threads, callbacks = std::move(callbacks), live_preview] {
            impl_->run(model_path, language, threads, callbacks, live_preview);
        });
    } catch (...) {
        impl_->running = false;
        throw;
    }
    return true;
}

void Engine::stop() { impl_->stop_requested = true; }
void Engine::cancel() {
    impl_->cancel_requested = true;
    impl_->stop_requested = true;
}
bool Engine::busy() const noexcept { return impl_->running; }

std::string Engine::transcribe(const std::string& model_path, const std::vector<float>& samples,
                               const std::string& language, int threads) {
    return transcribe_samples(model_path, samples, language, threads, nullptr);
}

std::string Engine::transcribe_file(const std::string& model_path, const std::string& wav_path,
                                    const std::string& language, int threads) {
    return transcribe(model_path, read_wav(wav_path), language, threads);
}

} // namespace lilt
