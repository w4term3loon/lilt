// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace lilt::detail {
namespace fs = std::filesystem;

inline bool path_present(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error && status.type() != fs::file_type::not_found)
        throw fs::filesystem_error("Cannot inspect migration path", path, error);
    return status.type() != fs::file_type::not_found;
}

inline void private_directory(const fs::path& path) {
    if (path_present(path)) {
        if (!fs::is_directory(fs::symlink_status(path)))
            throw std::runtime_error("Migration destination is not a directory: " + path.string());
        return;
    }
    fs::create_directories(path);
    fs::permissions(path, fs::perms::owner_all);
}

// Copies are staged beside the destination and published without replacing it.
// Configuration must be copied: sharing its inode would modify the old settings.
// Models are immutable; the downloader repairs them using atomic replacement.
inline void import_file(const fs::path& source, const fs::path& target, bool link_model) {
    if (path_present(target)) return;
    private_directory(target.parent_path());
    if (link_model) {
        std::error_code error;
        fs::create_hard_link(source, target, error);
        if (!error || error == std::errc::file_exists) return;
        // A separate filesystem, or restricted hardlinks, needs a private copy.
    }
    auto pattern = (target.parent_path() / ".lilt-migrate-XXXXXX").string();
    std::vector<char> filename(pattern.begin(), pattern.end());
    filename.push_back('\0');
    const int fd = mkstemp(filename.data());
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "Cannot stage legacy data");
    close(fd);
    const fs::path temporary(filename.data());
    try {
        fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write);
        std::error_code error;
        fs::create_hard_link(temporary, target, error);
        if (error && error != std::errc::file_exists)
            throw fs::filesystem_error("Cannot publish legacy data", target, error);
        fs::remove(temporary);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

// Import only user settings and complete model files, never old executable code.
// Existing lilt files win; legacy files are never changed or removed. A marker
// prevents models deliberately removed after migration from returning on launch.
inline std::string migrate_legacy_state(const fs::path& config_home, const fs::path& data_home) {
    try {
        const auto marker = data_home / "lilt/.legacy-migration-complete";
        if (path_present(marker)) return {};
        const auto old_config = config_home / "ptt/config.ini";
        const auto old_models = data_home / "ptt/models";
        const bool has_config = path_present(old_config) && fs::is_regular_file(fs::symlink_status(old_config));
        const bool has_models = path_present(old_models) && fs::is_directory(fs::symlink_status(old_models));
        if (!has_config && !has_models) return {};

        if (has_config) import_file(old_config, config_home / "lilt/config.ini", false);
        if (has_models) {
            for (const auto& entry : fs::directory_iterator(old_models)) {
                const auto name = entry.path().filename().string();
                if (!fs::is_regular_file(entry.symlink_status()) || name.rfind("ggml-", 0) != 0 ||
                    entry.path().extension() != ".bin") continue;
                import_file(entry.path(), data_home / "lilt/models" / name, true);
            }
        }
        private_directory(marker.parent_path());
        // An empty marker is sufficient; never truncate an existing user file.
        auto pattern = (marker.parent_path() / ".lilt-migrate-XXXXXX").string();
        std::vector<char> filename(pattern.begin(), pattern.end());
        filename.push_back('\0');
        const int fd = mkstemp(filename.data());
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "Cannot finish legacy migration");
        close(fd);
        std::error_code error;
        fs::create_hard_link(filename.data(), marker, error);
        std::error_code ignored;
        fs::remove(filename.data(), ignored);
        if (error && error != std::errc::file_exists)
            throw fs::filesystem_error("Cannot finish legacy migration", marker, error);
        return {};
    } catch (const std::exception& error) {
        // Keep startup usable and retry on the next launch after the user fixes
        // a permission or storage failure. Completed imports remain untouched.
        return error.what();
    }
}
} // namespace lilt::detail
