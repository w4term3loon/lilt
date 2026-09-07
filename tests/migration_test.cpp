// SPDX-License-Identifier: GPL-3.0-or-later
#include "migration.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
namespace fs = std::filesystem;
using lilt::detail::migrate_legacy_state;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    fs::path path;
    explicit TemporaryDirectory(const fs::path& base = fs::temp_directory_path()) {
        const auto pattern = (base / "lilt-migration-test-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        const auto* created = mkdtemp(name.data());
        if (!created) throw std::system_error(errno, std::generic_category(), "Cannot create test directory");
        path = created;
    }
    ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};

void write(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << contents;
    expect(stream.good(), "Cannot write isolated fixture");
}

std::string read(const fs::path& path) {
    std::ifstream stream(path);
    expect(stream.good(), "Migrated file is missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void first_run() {
    TemporaryDirectory directory;
    const auto config = directory.path / "config", data = directory.path / "data";
    const auto old_config = config / "ptt/config.ini", current_config = config / "lilt/config.ini";
    const auto old_model = data / "ptt/models/ggml-small.en-q5_1.bin";
    const auto current_model = data / "lilt/models/ggml-small.en-q5_1.bin";
    const std::string settings = "[PTT]\nmodel=small.en-q5_1\nshortcut=<Control><Alt>space\n";
    write(old_config, settings);
    write(old_model, "fixture model");
    write(data / "ptt/download-model.py", "obsolete code must not migrate");
    write(data / "ptt/models/.unfinished.bin.part", "incomplete");
    fs::create_symlink(old_model, data / "ptt/models/ggml-symlink.bin");

    expect(migrate_legacy_state(config, data).empty(), "First migration failed");
    expect(read(current_config) == settings, "Original settings were not preserved");
    expect(!fs::equivalent(old_config, current_config), "Settings must not share the old inode");
    expect(fs::equivalent(old_model, current_model), "Models should use hardlinks on the same filesystem");
    expect(!fs::exists(data / "lilt/download-model.py"), "Old executable code must not migrate");
    expect(!fs::exists(data / "lilt/models/.unfinished.bin.part"), "Partial models must not migrate");
    expect(!fs::exists(data / "lilt/models/ggml-symlink.bin"), "Model symlinks must not be followed");
    write(current_config, "new settings");
    fs::remove(current_model);
    expect(migrate_legacy_state(config, data).empty(), "Repeated migration failed");
    expect(!fs::exists(current_model), "One-time migration must not restore a deliberately removed model");
    expect(read(old_config) == settings && read(old_model) == "fixture model", "Migration changed legacy data");
    expect(read(current_config) == "new settings", "Migration overwrote new settings");
}

void existing_state_wins() {
    TemporaryDirectory directory;
    const auto config = directory.path / "config", data = directory.path / "data";
    write(config / "ptt/config.ini", "old settings");
    write(config / "lilt/config.ini", "current settings");
    write(data / "ptt/models/ggml-existing.bin", "old model");
    write(data / "lilt/models/ggml-existing.bin", "current model");
    write(data / "ptt/models/ggml-blocked.bin", "old model");
    fs::create_symlink(data / "missing", data / "lilt/models/ggml-blocked.bin");
    expect(migrate_legacy_state(config, data).empty(), "Migration around existing state failed");
    expect(read(config / "lilt/config.ini") == "current settings", "Existing settings were overwritten");
    expect(read(data / "lilt/models/ggml-existing.bin") == "current model", "Existing model was overwritten");
    expect(fs::is_symlink(fs::symlink_status(data / "lilt/models/ggml-blocked.bin")),
           "Even a dangling destination symlink must be preserved");
}

void retry_after_failure() {
    TemporaryDirectory directory;
    const auto config = directory.path / "config", data = directory.path / "data";
    write(config / "ptt/config.ini", "old settings");
    write(config / "lilt", "user file blocks destination directory");
    expect(!migrate_legacy_state(config, data).empty(), "An obstructed migration must report a failure");
    expect(!fs::exists(data / "lilt/.legacy-migration-complete"), "Failed migration must remain retryable");
    expect(read(config / "lilt") == "user file blocks destination directory", "Failure overwrote a user file");
    fs::remove(config / "lilt");
    expect(migrate_legacy_state(config, data).empty(), "Migration did not recover after destination was fixed");
    expect(read(config / "lilt/config.ini") == "old settings", "Recovered migration lost settings");
}

void cross_filesystem_copy() {
    // /dev/shm is optional, but lets Linux CI exercise an actual EXDEV fallback.
    if (!fs::is_directory("/dev/shm") || access("/dev/shm", W_OK) != 0) return;
    TemporaryDirectory source;
    TemporaryDirectory target("/dev/shm");
    write(source.path / "model.bin", "cross-device model");
    lilt::detail::import_file(source.path / "model.bin", target.path / "models/model.bin", true);
    expect(read(target.path / "models/model.bin") == "cross-device model", "Copy fallback lost the model");
    expect(read(source.path / "model.bin") == "cross-device model", "Copy fallback changed the source");
}
} // namespace

int main() {
    try {
        first_run();
        existing_state_wins();
        retry_after_failure();
        cross_filesystem_copy();
        std::cout << "Legacy migration passed: preservation, one-time import, failure recovery, and copy fallback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
