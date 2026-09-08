#include "engine.hpp"
#include "text.hpp"
#include "streaming_text.hpp"
#include "audio_bands.hpp"
#include "migration.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
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
        char pattern[] = "/tmp/ren-engine-test-XXXXXX";
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
    using ren::normalize_dictation;
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
    using ren::detail::stable_word_prefix;
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

    ren::detail::LatestValue<int> pending;
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

    ren::detail::LatestValue<int> waiting;
    auto consumer = std::async(std::launch::async, [&] {
        return waiting.take(std::chrono::steady_clock::now() + 60s);
    });
    const bool waited = consumer.wait_for(20ms) == std::future_status::timeout;
    waiting.close();
    expect(waited && consumer.wait_for(1s) == std::future_status::ready && !consumer.get(),
           "Finishing wakes an idle decoder without supplying stale work");
}

void audio_tests(const std::filesystem::path& directory) {
    ren::Engine engine;
    const auto missing = (directory / "missing.bin").string();
    expect(engine.transcribe(missing, {}).empty(), "Empty audio skips the model");
    expect(engine.transcribe(missing, std::vector<float>(32000, 0)).empty(), "Silence skips the model");
    expect(engine.transcribe(missing, std::vector<float>(32000, 0.00001f)).empty(), "Near-silence skips the model");
    expect(engine.transcribe(missing, std::vector<float>(100, 0.2f)).empty(), "Click-length audio is rejected");
    expect_error([&] { engine.transcribe(missing, std::vector<float>(3200, 0.2f)); },
                 "Audible input requires a model");
    expect_error([&] { engine.transcribe(missing, {std::numeric_limits<float>::quiet_NaN()}); },
                 "Nonfinite samples rejected");
    expect_error([&] { engine.transcribe(missing, std::vector<float>(16000 * 180 + 1)); },
                 "Long audio rejected before model load");

    const auto filename = directory / "input.wav";
    write(filename, wav());
    expect(engine.transcribe_file(missing, filename.string()).empty(), "PCM16 silence WAV accepted");
    write(filename, wav(3, 16000, 1, 32));
    expect(engine.transcribe_file(missing, filename.string()).empty(), "Float32 silence WAV accepted");
    write(filename, wav(1, 16000, 1, 16, 3200, true));
    expect(engine.transcribe_file(missing, filename.string()).empty(), "Odd metadata chunk padding accepted");
    write(filename, wav(1, 44100));
    expect_error([&] { engine.transcribe_file(missing, filename.string()); }, "Wrong sample rate rejected");
    write(filename, wav(1, 16000, 2));
    expect_error([&] { engine.transcribe_file(missing, filename.string()); }, "Stereo rejected explicitly");
    write(filename, wav(1, 16000, 1, 8));
    expect_error([&] { engine.transcribe_file(missing, filename.string()); }, "Unsupported sample format rejected");
    auto truncated = wav();
    truncated.resize(truncated.size() - 1);
    write(filename, truncated);
    expect_error([&] { engine.transcribe_file(missing, filename.string()); }, "Truncated data rejected");
    auto malformed = wav();
    malformed[40] = malformed[41] = malformed[42] = malformed[43] = 0xff;
    write(filename, malformed);
    expect_error([&] { engine.transcribe_file(missing, filename.string()); }, "Oversized chunk rejected before allocation");
    write(filename, {'n', 'o', 't', ' ', 'w', 'a', 'v'});
    expect_error([&] { engine.transcribe_file(missing, filename.string()); }, "Short invalid file rejected");
    expect_error([&] { engine.transcribe_file(missing, (directory / "missing.wav").string()); },
                 "Missing WAV rejected");
    expect(!engine.busy(), "WAV read errors release the replay operation");
    expect(engine.transcribe(missing, {}).empty(), "A failed file read does not strand later replay");
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
    ren::Engine::Callbacks callbacks;
    callbacks.on_state = [&](std::string state, std::string) {
        if (state == "idle") ++terminal;
        if (state == "error") ++errors;
    };
    callbacks.on_result = [&](std::string) { ++results; };
    const auto started = std::chrono::steady_clock::now();
    {
        ren::Engine engine;
        for (int i = 0; i < 2; ++i) {
            expect(engine.start(model, 1, callbacks), "New session starts after cancel");
            std::this_thread::sleep_for(50ms);
            expect(engine.busy(), "Stalled audio setup remains cancellable");
            expect(!engine.start(model, 1, callbacks), "Overlapping session rejected");
            expect_error([&] { engine.transcribe(model, {}); }, "Replay rejects capture overlap");
            engine.release_model(); // Clearing a cache must not wait for capture.
            engine.cancel();
            wait_until([&] { return !engine.busy(); }, 1s,
                       "Cancel must not wait on a stalled audio server");
        }
    }
    expect(terminal == 2 && errors == 0 && results == 0, "Cancelled sessions must never produce text");
    expect(std::chrono::steady_clock::now() - started < 2s, "Repeated capture cancellation is bounded");
    {
        ren::Engine engine;
        expect(engine.start(model, 1, callbacks), "Destructor test starts");
        std::this_thread::sleep_for(50ms);
    } // Destruction cancels and joins a stalled capture.
    expect(std::chrono::steady_clock::now() - started < 3s, "Destruction must release the worker promptly");
}

void migration_tests(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    const auto config = directory / "config", data = directory / "data";
    auto put = [](const fs::path& path, const char* value) {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << value;
    };
    auto read = [](const fs::path& path) {
        std::ifstream stream(path);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    put(config / "lilt/config.ini", "[lilt]\nmodel=custom\ncustom_model=/models/personal.bin\n");
    put(config / "ptt/config.ini", "[PTT]\nmodel=tiny-q5_1\n");
    put(data / "lilt/models/ggml-shared.bin", "lilt");
    put(data / "ptt/models/ggml-shared.bin", "ptt");
    put(data / "ptt/models/ggml-fallback.bin", "fallback");
    put(data / "lilt/models/ggml-existing.bin", "old");
    put(data / "ren/models/ggml-existing.bin", "current");
    put(data / "lilt/models/ggml-unfinished.bin.part", "partial");
    expect(ren::detail::migrate_legacy_state(config, data).empty(), "Legacy import succeeds");
    expect(read(config / "ren/config.ini") == read(config / "lilt/config.ini"), "Lilt settings precede PTT");
    expect(!fs::equivalent(config / "ren/config.ini", config / "lilt/config.ini"), "Settings are copied independently");
    expect(read(data / "ren/models/ggml-shared.bin") == "lilt", "Lilt models precede PTT");
    expect(read(data / "ren/models/ggml-fallback.bin") == "fallback", "PTT fills missing models");
    expect(read(data / "ren/models/ggml-existing.bin") == "current", "Existing Ren models win");
    expect(!fs::exists(data / "ren/models/ggml-unfinished.bin.part"), "Incomplete downloads are skipped");
    fs::remove(data / "ren/models/ggml-shared.bin");
    expect(ren::detail::migrate_legacy_state(config, data).empty(), "Repeated import succeeds");
    expect(!fs::exists(data / "ren/models/ggml-shared.bin"), "Marker prevents deleted models returning");
    expect(read(data / "lilt/models/ggml-shared.bin") == "lilt", "Legacy models remain untouched");
    put(config / "ren/config.ini", "[ren]\nmodel=base.en-q5_1\n");
    fs::remove(data / "ren/.legacy-migration-complete");
    put(data / "lilt/.legacy-migration-complete", "");
    fs::remove(data / "ren/models/ggml-fallback.bin");
    expect(ren::detail::migrate_legacy_state(config, data).empty(), "Import can resume safely");
    expect(read(config / "ren/config.ini") == "[ren]\nmodel=base.en-q5_1\n", "Existing Ren settings win");
    expect(!fs::exists(data / "ren/models/ggml-fallback.bin"), "Completed Lilt migration suppresses old PTT models");
}

} // namespace

int main() {
    try {
        const std::array<double, 3> tones{150, 700, 3000};
        for (std::size_t band = 0; band < tones.size(); ++band) {
            ren::AudioBands meter;
            for (int sample = 0; sample < 16000; ++sample)
                meter.add(0.1 * std::sin(2 * 3.141592653589793 * tones[band] * sample / 16000));
            const auto levels = meter.take();
            for (std::size_t other = 0; other < levels.size(); ++other)
                if (other != band) expect(levels[band] > levels[other], "Tone activates its frequency band");
            expect(meter.take() == std::array<double, 3>{}, "Empty interval has no band energy");
        }
        ren::AudioBands silence;
        for (int sample = 0; sample < 1600; ++sample) silence.add(0);
        expect(silence.take() == std::array<double, 3>{}, "Silence leaves all bands still");
        TemporaryDirectory directory;
        text_tests();
        preview_tests();
        audio_tests(directory.path);
        lifecycle_tests(directory.path);
        migration_tests(directory.path);
        std::cout << "Text, audio, capture lifecycle, and settings/model migration checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
