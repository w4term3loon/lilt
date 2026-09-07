#include "engine.hpp"
#include "text.hpp"
#include "streaming_text.hpp"

#include <pulse/pulseaudio.h>
#include <whisper.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace {
using namespace std::chrono_literals;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Callable>
void expect_error(Callable action, const std::string& message) {
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    expect(failed, message);
}

template<class Predicate>
void wait_until(Predicate ready, std::chrono::milliseconds timeout, const std::string& message) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!ready() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    expect(ready(), message);
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        char pattern[] = "/tmp/lilt-engine-test-XXXXXX";
        const char* made = mkdtemp(pattern);
        if (!made) throw std::runtime_error("Cannot create test directory");
        path = made;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};

void append16(std::vector<unsigned char>& bytes, std::uint16_t value) {
    bytes.push_back(value & 0xff);
    bytes.push_back((value >> 8) & 0xff);
}

void append32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    append16(bytes, value & 0xffff);
    append16(bytes, (value >> 16) & 0xffff);
}

void append_tag(std::vector<unsigned char>& bytes, const char* tag) {
    bytes.insert(bytes.end(), tag, tag + 4);
}

std::vector<unsigned char> wav(std::uint16_t format = 1, std::uint32_t rate = 16000,
                                std::uint16_t channels = 1, std::uint16_t bits = 16,
                                std::uint32_t count = 3200, bool odd_chunk = false) {
    std::vector<unsigned char> bytes;
    append_tag(bytes, "RIFF");
    const auto data_bytes = count * channels * (bits / 8);
    append32(bytes, 36 + data_bytes + (odd_chunk ? 10 : 0));
    append_tag(bytes, "WAVE");
    if (odd_chunk) {
        append_tag(bytes, "JUNK");
        append32(bytes, 1);
        bytes.push_back(42);
        bytes.push_back(0);
    }
    append_tag(bytes, "fmt ");
    append32(bytes, 16);
    append16(bytes, format);
    append16(bytes, channels);
    append32(bytes, rate);
    append32(bytes, rate * channels * (bits / 8));
    append16(bytes, channels * (bits / 8));
    append16(bytes, bits);
    append_tag(bytes, "data");
    append32(bytes, data_bytes);
    bytes.resize(bytes.size() + data_bytes, 0);
    return bytes;
}

void write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    expect(static_cast<bool>(file), "Could not write test WAV");
}

void text_tests() {
    using lilt::normalize_dictation;
    expect(normalize_dictation("  hello\r\n\tworld  ") == "hello world", "Return/Tab normalization");
    expect(normalize_dictation(std::string("left\0right", 10)) == "left right", "Embedded NUL normalization");
    expect(normalize_dictation(u8"árvíztűrő tükörfúrógép 日本語 👋") == u8"árvíztűrő tükörfúrógép 日本語 👋",
           "Multilingual UTF-8 preservation");
    expect(normalize_dictation(u8"a\u0085b\u2028c\u2029d\u00a0e\u3000f") == "a b c d e f",
           "Unicode line separator normalization");
    expect(normalize_dictation(u8"\ufefftext") == "text", "Leading BOM normalization");
    expect(normalize_dictation(std::string("a\xf0\x80\x80\x80" "b")) == "a b", "Overlong UTF-8 rejected");
    expect(normalize_dictation(std::string("a\xed\xa0\x80" "b")) == "a b", "Surrogate UTF-8 rejected");
    expect(normalize_dictation(std::string("a\xe2\x82")) == "a", "Truncated UTF-8 rejected");
    std::string controls = "a";
    for (int i = 0; i < 32; ++i) controls.push_back(static_cast<char>(i));
    controls.push_back(127);
    controls += 'b';
    expect(normalize_dictation(controls) == "a b", "Every ASCII control must be removed");
    expect(normalize_dictation("\r\n\t\033\177").empty(), "Controls alone are empty");
}

void preview_tests() {
    using lilt::detail::stable_word_prefix;
    expect(stable_word_prefix("", "hello world") == 0, "First hypothesis is entirely tentative");
    expect(stable_word_prefix("hello world", "hello world") == 6, "Final word stays tentative");
    expect(stable_word_prefix("I like cats", "I like dogs") == 7, "Changed word is excluded from stable prefix");
    expect(stable_word_prefix("I like dogs", "I love dogs") == 2, "A corrected prefix may shrink");
    expect(stable_word_prefix("hello", "hello world") == 0, "A word without a shared terminator stays tentative");
    expect(stable_word_prefix(u8"árvíz á", u8"árvíz é") == std::string(u8"árvíz ").size(),
           "A common leading UTF-8 byte must never become a split character boundary");
    expect(stable_word_prefix(u8"日本語", u8"日本人") == 0, "Unsegmented text remains tentative");
    expect(stable_word_prefix(u8"日本語 text", u8"日本語 test") == std::string(u8"日本語 ").size(),
           "A complete multibyte prefix retains a valid byte offset");

    lilt::detail::LatestValue<int> pending;
    pending.publish(1);
    expect(pending.take() == 1, "Worker consumes first snapshot");
    pending.publish(2);
    pending.publish(3);
    expect(pending.take() == 3, "Only newest pending snapshot survives while worker is busy");
    pending.publish(4);
    pending.close();
    expect(!pending.take(), "Finishing drops pending partial work");
    pending.publish(5);
    expect(!pending.take(), "A closed preview queue cannot restart");

    lilt::detail::LatestValue<int> waiting;
    auto consumer = std::async(std::launch::async, [&] {
        return waiting.take(std::chrono::steady_clock::now() + 60s);
    });
    const bool waited = consumer.wait_for(20ms) == std::future_status::timeout;
    waiting.close();
    expect(waited && consumer.wait_for(1s) == std::future_status::ready && !consumer.get(),
           "Finishing wakes an idle decoder without supplying stale work");
}

void audio_tests(const std::filesystem::path& directory) {
    const auto missing = (directory / "missing.bin").string();
    expect(lilt::Engine::transcribe(missing, {}).empty(), "Empty audio skips the model");
    expect(lilt::Engine::transcribe(missing, std::vector<float>(32000, 0)).empty(), "Silence skips the model");
    expect(lilt::Engine::transcribe(missing, std::vector<float>(32000, 0.00001f)).empty(), "Near-silence skips the model");
    expect(lilt::Engine::transcribe(missing, std::vector<float>(100, 0.2f)).empty(), "Click-length audio is rejected");
    expect_error([&] { lilt::Engine::transcribe(missing, std::vector<float>(3200, 0.2f)); },
                 "Audible input requires a model");
    expect_error([&] { lilt::Engine::transcribe(missing, std::vector<float>(3200, 0.2f), "not-a-language"); },
                 "Invalid language rejected");
    expect_error([&] { lilt::Engine::transcribe(missing, {std::numeric_limits<float>::quiet_NaN()}); },
                 "Nonfinite samples rejected");
    expect_error([&] { lilt::Engine::transcribe(missing, std::vector<float>(16000 * 180 + 1)); },
                 "Long audio rejected before model load");

    const auto filename = directory / "input.wav";
    write(filename, wav());
    expect(lilt::Engine::transcribe_file(missing, filename.string()).empty(), "PCM16 silence WAV accepted");
    write(filename, wav(3, 16000, 1, 32));
    expect(lilt::Engine::transcribe_file(missing, filename.string()).empty(), "Float32 silence WAV accepted");
    write(filename, wav(1, 16000, 1, 16, 3200, true));
    expect(lilt::Engine::transcribe_file(missing, filename.string()).empty(), "Odd metadata chunk padding accepted");
    write(filename, wav(1, 44100));
    expect_error([&] { lilt::Engine::transcribe_file(missing, filename.string()); }, "Wrong sample rate rejected");
    write(filename, wav(1, 16000, 2));
    expect_error([&] { lilt::Engine::transcribe_file(missing, filename.string()); }, "Stereo rejected explicitly");
    write(filename, wav(1, 16000, 1, 8));
    expect_error([&] { lilt::Engine::transcribe_file(missing, filename.string()); }, "Unsupported sample format rejected");
    auto truncated = wav();
    truncated.resize(truncated.size() - 1);
    write(filename, truncated);
    expect_error([&] { lilt::Engine::transcribe_file(missing, filename.string()); }, "Truncated data rejected");
    auto malformed = wav();
    malformed[40] = malformed[41] = malformed[42] = malformed[43] = 0xff;
    write(filename, malformed);
    expect_error([&] { lilt::Engine::transcribe_file(missing, filename.string()); }, "Oversized chunk rejected before allocation");
    write(filename, {'n', 'o', 't', ' ', 'w', 'a', 'v'});
    expect_error([&] { lilt::Engine::transcribe_file(missing, filename.string()); }, "Short invalid file rejected");
    expect_error([&] { lilt::Engine::transcribe_file(missing, (directory / "missing.wav").string()); },
                 "Missing WAV rejected");
}

// A listening socket that never completes PulseAudio authentication exercises
// cancellation while the audio server is stalled. No real microphone is used.
struct StalledAudioServer {
    int socket = -1;
    std::string previous;
    bool had_previous = false;
    explicit StalledAudioServer(const std::filesystem::path& directory) {
        if (const char* value = std::getenv("PULSE_SERVER")) {
            previous = value;
            had_previous = true;
        }
        socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
        expect(socket >= 0, "Cannot create fake audio socket");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path = (directory / "pulse.sock").string();
        expect(path.size() < sizeof(address.sun_path), "Test socket path is too long");
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        expect(bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
               "Cannot bind fake audio socket");
        expect(listen(socket, 8) == 0, "Cannot listen on fake audio socket");
        expect(setenv("PULSE_SERVER", ("unix:" + path).c_str(), 1) == 0, "Cannot configure fake audio server");
    }
    ~StalledAudioServer() {
        if (socket >= 0) close(socket);
        if (had_previous) setenv("PULSE_SERVER", previous.c_str(), 1);
        else unsetenv("PULSE_SERVER");
    }
};

void lifecycle_tests(const std::filesystem::path& directory) {
    StalledAudioServer server(directory);
    const auto model = (directory / "placeholder-model.bin").string();
    std::ofstream(model).put(0); // Capture setup checks readability, not model contents.
    std::atomic<int> results{0}, terminal{0}, errors{0};
    lilt::Engine::Callbacks callbacks;
    callbacks.on_state = [&](std::string state, std::string) {
        if (state == "idle") ++terminal;
        if (state == "error") ++errors;
    };
    callbacks.on_result = [&](std::string) { ++results; };
    const auto started = std::chrono::steady_clock::now();
    {
        lilt::Engine engine;
        for (int i = 0; i < 3; ++i) {
            expect(engine.start(model, "en", 1, callbacks), "New session starts after cancel");
            std::this_thread::sleep_for(50ms);
            expect(engine.busy(), "Stalled audio setup remains cancellable");
            expect(!engine.start(model, "en", 1, callbacks), "Overlapping session rejected");
            engine.cancel();
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (engine.busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
            expect(!engine.busy(), "Cancel must not wait on a stalled audio server");
        }
    }
    expect(terminal == 3 && errors == 0 && results == 0, "Cancelled sessions must never produce text");
    expect(std::chrono::steady_clock::now() - started < 2s, "Repeated capture cancellation is bounded");
    {
        lilt::Engine engine;
        expect(engine.start(model, "en", 1, callbacks), "Destructor test starts");
        std::this_thread::sleep_for(50ms);
    } // Destruction cancels and joins a stalled capture.
    expect(std::chrono::steady_clock::now() - started < 3s, "Destruction must release the worker promptly");
}

// Use Whisper's existing log callback to hold initialization before any model
// data is read. This makes overlap and cancellation deterministic without a
// production-only delay or a substitute inference implementation.
struct ModelLoadProbe {
    std::atomic<bool>& recording;
    const bool hold;
    std::atomic<int> loads{0};
    std::atomic<bool> entered{false}, release{false}, during_recording{false}, timed_out{false};

    ModelLoadProbe(std::atomic<bool>& active, bool block = false) : recording(active), hold(block) {
        whisper_log_set(+[](ggml_log_level, const char* text, void* user) {
            auto& self = *static_cast<ModelLoadProbe*>(user);
            if (!std::strstr(text, "loading model from '")) return;
            ++self.loads;
            self.during_recording = self.recording.load();
            self.entered = true;
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (self.hold && !self.release && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(5ms);
            if (self.hold && !self.release) self.timed_out = true;
        }, this);
    }
    ~ModelLoadProbe() { whisper_log_set(nullptr, nullptr); }
};

struct ReleaseLoad {
    ModelLoadProbe& probe;
    ~ReleaseLoad() { probe.release = true; }
};

// Metadata only: count lilt recording streams on the explicitly named synthetic
// monitor. Never open an audio stream or inspect the user's real microphone.
struct CaptureInspector {
    std::unique_ptr<pa_mainloop, decltype(&pa_mainloop_free)> loop{pa_mainloop_new(), pa_mainloop_free};
    std::unique_ptr<pa_context, decltype(&pa_context_unref)> context{nullptr, pa_context_unref};
    std::uint32_t source = PA_INVALID_INDEX;

    explicit CaptureInspector(const char* source_name) {
        expect(static_cast<bool>(loop), "Cannot create Pulse metadata loop");
        context.reset(pa_context_new(pa_mainloop_get_api(loop.get()), "lilt integration metadata"));
        expect(context && pa_context_connect(context.get(), nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) == 0,
               "Cannot connect for Pulse metadata");
        wait_until([&] {
            pump();
            return pa_context_get_state(context.get()) == PA_CONTEXT_READY;
        }, 3s, "Pulse metadata connection did not become ready");
        operation(pa_context_get_source_info_by_name(context.get(), source_name,
            +[](pa_context*, const pa_source_info* info, int, void* user) {
                if (info) *static_cast<std::uint32_t*>(user) = info->index;
            }, &source));
        expect(source != PA_INVALID_INDEX, "Synthetic monitor source is missing");
    }
    ~CaptureInspector() { if (context) pa_context_disconnect(context.get()); }

    void pump() {
        expect(pa_mainloop_iterate(loop.get(), 0, nullptr) >= 0 &&
               PA_CONTEXT_IS_GOOD(pa_context_get_state(context.get())), "Pulse metadata connection failed");
    }
    void operation(pa_operation* raw) {
        std::unique_ptr<pa_operation, decltype(&pa_operation_unref)> pending(raw, pa_operation_unref);
        expect(static_cast<bool>(pending), "Cannot request Pulse metadata");
        try {
            wait_until([&] {
                pump();
                return pa_operation_get_state(pending.get()) != PA_OPERATION_RUNNING;
            }, 3s, "Pulse metadata request timed out");
        } catch (...) {
            pa_operation_cancel(pending.get());
            throw;
        }
    }
    int streams() {
        struct Result { std::uint32_t source; int count = 0; bool failed = false; } result{source};
        operation(pa_context_get_source_output_info_list(context.get(),
            +[](pa_context*, const pa_source_output_info* info, int eol, void* user) {
                auto& result = *static_cast<Result*>(user);
                if (eol < 0) result.failed = true;
                if (info && info->source == result.source && info->name &&
                    std::strcmp(info->name, "Dictation microphone") == 0) ++result.count;
            }, &result));
        expect(!result.failed, "Cannot inspect synthetic recording streams");
        return result.count;
    }
};

void preparation_integration(const std::string& model, CaptureInspector& inspector) {
    TemporaryDirectory directory;
    const auto corrupt = (directory.path / "corrupt-model.bin").string();
    std::ofstream(corrupt).write("invalid model", 13);
    for (const std::string scenario : {"stop-on-ready", "load-error", "cancel-loading"}) {
        std::atomic<bool> recording{false}, finished{false};
        std::atomic<int> levels{0}, partials{0};
        std::string output, terminal_state, terminal_message;
        ModelLoadProbe probe(recording, scenario == "cancel-loading");
        lilt::Engine engine;
        ReleaseLoad unblock{probe}; // Release the loader before Engine unwinds on a failed assertion.
        lilt::Engine::Callbacks callbacks;
        callbacks.on_result = [&](std::string text) { output = std::move(text); };
        callbacks.on_level = [&](double) { ++levels; };
        callbacks.on_partial = [&](std::string, std::size_t) { ++partials; };
        callbacks.on_state = [&](std::string state, std::string message) {
            recording = state == "recording";
            if (recording && scenario == "stop-on-ready") engine.stop();
            if (state == "idle" || state == "error") {
                terminal_state = std::move(state);
                terminal_message = std::move(message);
                finished = true;
            }
        };
        const auto& selected = scenario == "cancel-loading" ? model : corrupt;
        expect(engine.start(selected, "en", 4, callbacks, true), "Preparation test starts");
        if (scenario == "cancel-loading") {
            wait_until([&] { return probe.entered.load(); }, 3s, "Model load did not begin during recording");
            expect(probe.during_recording && recording, "Microphone must record before model initialization");
            wait_until([&] { return levels >= 2; }, 1s, "Audio levels must update while initialization is blocked");
            expect(inspector.streams() == 1, "Synthetic microphone must stay open while loading");
            engine.cancel();
            wait_until([&] { return inspector.streams() == 0; }, 1s,
                       "Cancellation must close the microphone without waiting for model initialization");
            expect(engine.busy() && !finished, "Cancellation must join the outstanding model initializer");
            probe.release = true;
        } else if (scenario == "load-error") {
            wait_until([&] { return probe.entered.load(); }, 3s, "Invalid model initialization did not start");
            wait_until([&] { return finished.load(); }, 1s,
                       "Model initialization failure must stop capture promptly without Finish");
        }
        wait_until([&] { return finished.load(); }, 10s, "Preparation test failed to finish");
        expect(output.empty(), "Preparation failure, silence, and cancellation must not emit text");
        expect(partials == 0, "Preparation failure and cancellation before speech must not emit a preview");
        // PipeWire removes a disconnected Pulse stream asynchronously.
        wait_until([&] { return inspector.streams() == 0; }, 1s,
                   "Terminal state must own no microphone stream: " + scenario);
        expect(!probe.timed_out, "Model initialization test gate timed out");
        if (scenario == "load-error") {
            expect(probe.loads == 1 && probe.during_recording, "Load errors must surface during capture");
            expect(terminal_state == "error" && terminal_message.find("Cannot load the model") != std::string::npos,
                   "Model initialization error was not reported");
        } else if (scenario == "stop-on-ready") {
            expect(probe.loads == 0 && terminal_state == "idle" && terminal_message == "No speech detected",
                   "Finish from the recording callback must skip loading and decoding safely");
        } else {
            expect(probe.loads == 1 && terminal_state == "idle" && terminal_message == "Cancelled",
                   "Cancellation during loading must join exactly one initializer and suppress text");
        }
    }
    std::cout << "Background initialization: capture overlap, immediate Stop, load error, and cancellation passed.\n";
}

void capture_integration(const std::string& model, const std::string& fixture,
                          const std::string& player) {
    // Opt-in only. Route playback and capture to a caller-created null sink;
    // refuse default audio devices so this test cannot record a real microphone.
    const char* source = std::getenv("PULSE_SOURCE");
    const char* sink = std::getenv("PULSE_SINK");
    expect(source && sink && std::string(sink).rfind("lilt_test_", 0) == 0 &&
           std::string(source) == std::string(sink) + ".monitor",
           "Integration requires PULSE_SINK=lilt_test_NAME and PULSE_SOURCE=lilt_test_NAME.monitor");

    CaptureInspector inspector(source);
    preparation_integration(model, inspector);

    for (bool cancel_decode : {false, true}) {
        std::atomic<bool> recording{false}, transcribing{false}, finished{false};
        std::atomic<int> partials{0}, results{0};
        std::atomic<bool> valid_partials{true};
        std::string output, terminal_state, terminal_message;
        ModelLoadProbe probe(recording);
        lilt::Engine engine;
        lilt::Engine::Callbacks callbacks;
        callbacks.on_result = [&](std::string text) { output = std::move(text); ++results; };
        callbacks.on_partial = [&](std::string text, std::size_t stable) {
            if (stable > text.size() || (stable < text.size() &&
                (static_cast<unsigned char>(text[stable]) & 0xc0) == 0x80) ||
                text != lilt::normalize_dictation(text)) valid_partials = false;
            if (!text.empty()) ++partials;
        };
        callbacks.on_state = [&](std::string state, std::string message) {
            recording = state == "recording";
            if (state == "transcribing") transcribing = true;
            if (state == "idle" || state == "error") {
                terminal_state = std::move(state);
                terminal_message = std::move(message);
                finished = true;
            }
        };
        expect(engine.start(model, "en", 4, callbacks, !cancel_decode), "Synthetic capture starts");
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!recording && !finished && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        expect(recording, "Synthetic source must become ready");

        pid_t child = -1;
        char* args[] = {const_cast<char*>(player.c_str()), const_cast<char*>(fixture.c_str()), nullptr};
        expect(posix_spawn(&child, player.c_str(), nullptr, nullptr, args, environ) == 0,
               "Could not start synthetic audio playback");
        int status = 0;
        while (waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) throw std::runtime_error("Could not wait for audio playback");
        }
        expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Synthetic audio playback failed");
        std::this_thread::sleep_for(200ms);
        expect(results == 0, "Preview mode must not produce a final result before Finish");
        expect(valid_partials, "Partial text must be normalized UTF-8 with an intact stable boundary");
        if (!cancel_decode) expect(partials > 0, "Synthetic capture must produce a partial hypothesis before Finish");
        else expect(partials == 0, "Preview opt-out must not perform partial transcription");
        const auto stopped = std::chrono::steady_clock::now();
        engine.stop();
        if (cancel_decode) {
            deadline = std::chrono::steady_clock::now() + 5s;
            while (!transcribing && !finished && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(10ms);
            expect(transcribing, "Synthetic audio must reach inference before cancellation");
            // The small model loads in much less than this on the test machine;
            // inference itself takes several seconds, exercising ggml's abort.
            std::this_thread::sleep_for(500ms);
            engine.cancel();
        }
        deadline = std::chrono::steady_clock::now() + (cancel_decode ? 3s : 60s);
        while (!finished && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        expect(finished, "Synthetic transcription did not finish within the time limit");
        expect(terminal_state == "idle", "Synthetic transcription failed: " + terminal_message);
        expect(probe.loads == 1 && probe.during_recording,
               "Capture must initialize once during recording and reuse that context for decoding");
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stopped).count();
        if (cancel_decode) {
            expect(output.empty() && terminal_message == "Cancelled", "Cancelled inference must never emit text");
            std::cout << "Inference cancellation: no text emitted; " << elapsed << " s after recording stopped.\n";
        } else {
            expect(output.find("my fellow Americans") != std::string::npos,
                   "Synthetic microphone transcription did not recognize the fixture: " + output);
            expect(output == lilt::normalize_dictation(output), "Captured transcript must remain a safe single line");
            std::cout << "Synthetic microphone: " << output << "\nTranscription after stop: " << elapsed << " s.\n";
            std::cout << "Live preview: " << partials << " nonempty hypotheses; no final text before Finish.\n";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--capture") {
            capture_integration(argv[2], argv[3], argv[4]);
            return 0;
        }
        expect(argc == 1, "Usage: engine_test [--capture MODEL.bin JFK.wav /path/to/paplay]");
        TemporaryDirectory directory;
        text_tests();
        preview_tests();
        audio_tests(directory.path);
        lifecycle_tests(directory.path);
        std::cout << "Text safety, preview boundaries/coalescing, WAV validation, silence gating, and capture cancellation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
