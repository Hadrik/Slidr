#include "services/FilesystemService.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_littlefs.h"
#include "esp_err.h"
#include "esp_log.h"

namespace slidr::services {

namespace {
constexpr const char* kTag = "FilesystemService";

bool starts_with_root(const std::string& user_path, const std::string& root) {
    if (user_path == root) {
        return true;
    }
    return user_path.starts_with(root + "/");
}

core::Result stat_real_path(const std::string& user_path, const std::string& real_path, FileMetadata& out_metadata) {
    struct stat path_stat {};
    if (::stat(real_path.c_str(), &path_stat) != 0) {
        if (errno == ENOENT) {
            return core::Result::Failure(core::ErrorCode::FileNotFound, "Path not found: " + user_path);
        }

        return core::Result::Failure(
            core::ErrorCode::FileSystemError,
            "Failed to stat path '" + user_path + "': errno=" + std::to_string(errno));
    }

    out_metadata.path = user_path;
    out_metadata.size = static_cast<std::size_t>(path_stat.st_size);
    out_metadata.modified_ms = static_cast<uint64_t>(path_stat.st_mtime) * 1000ULL;
    out_metadata.is_directory = S_ISDIR(path_stat.st_mode);

    return core::Result::Success();
}

}  // namespace

core::Result FilesystemService::initialize() {
    if (_ready) {
        return core::Result::Success();
    }

    esp_vfs_littlefs_conf_t config = {
        .base_path = kMountPath,
        .partition_label = kPartitionLabel,
        .partition = nullptr,
        .format_if_mount_failed = true,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = true,
    };

    const esp_err_t mount_result = esp_vfs_littlefs_register(&config);
    if (mount_result != ESP_OK && mount_result != ESP_ERR_INVALID_STATE) {
        return core::Result::Failure(
            core::ErrorCode::FileSystemUnavailable,
            "Failed to mount littlefs: " + std::string(esp_err_to_name(mount_result)));
    }

    if (::mkdir("/littlefs/config", 0755) != 0 && errno != EEXIST) {
        return core::Result::Failure(
            core::ErrorCode::FileSystemError,
            "Failed to create /config directory: errno=" + std::to_string(errno));
    }

    if (::mkdir("/littlefs/images", 0755) != 0 && errno != EEXIST) {
        return core::Result::Failure(
            core::ErrorCode::FileSystemError,
            "Failed to create /images directory: errno=" + std::to_string(errno));
    }

    size_t total_bytes = 0;
    size_t used_bytes = 0;
    const esp_err_t info_result = esp_littlefs_info(kPartitionLabel, &total_bytes, &used_bytes);
    if (info_result == ESP_OK) {
        ESP_LOGI(kTag, "LittleFS ready: used=%u, total=%u", static_cast<unsigned>(used_bytes), static_cast<unsigned>(total_bytes));
    }

    _ready = true;
    return core::Result::Success();
}

core::Result FilesystemService::list(const std::string& user_path, std::vector<FileMetadata>& out_entries) const {
    out_entries.clear();

    if (!_ready) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not initialized");
    }

    const std::string sanitized_path = sanitize_user_path(user_path);
    if (sanitized_path == "/") {
        out_entries.push_back(FileMetadata {
            .path = "/config",
            .is_directory = true,
        });
        out_entries.push_back(FileMetadata {
            .path = "/images",
            .is_directory = true,
        });
        return core::Result::Success();
    }

    std::string real_path;
    core::Result resolve_result = resolve_path(sanitized_path, real_path);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    FileMetadata root_meta;
    core::Result stat_result = stat_real_path(sanitized_path, real_path, root_meta);
    if (!stat_result.ok()) {
        return stat_result;
    }

    if (!root_meta.is_directory) {
        out_entries.push_back(root_meta);
        return core::Result::Success();
    }

    DIR* dir = ::opendir(real_path.c_str());
    if (dir == nullptr) {
        return core::Result::Failure(
            core::ErrorCode::FileSystemError,
            "Failed to open directory '" + sanitized_path + "': errno=" + std::to_string(errno));
    }

    while (dirent* entry = ::readdir(dir)) {
        const std::string entry_name = entry->d_name;
        if (entry_name == "." || entry_name == "..") {
            continue;
        }

        const std::string child_user_path = join_user_path(sanitized_path, entry_name);
        std::string child_real_path;
        resolve_result = resolve_path(child_user_path, child_real_path);
        if (!resolve_result.ok()) {
            continue;
        }

        FileMetadata child_meta;
        stat_result = stat_real_path(child_user_path, child_real_path, child_meta);
        if (stat_result.ok()) {
            out_entries.push_back(std::move(child_meta));
        }
    }

    ::closedir(dir);
    return core::Result::Success();
}

core::Result FilesystemService::stat(const std::string& user_path, FileMetadata& out_metadata) const {
    if (!_ready) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not initialized");
    }

    std::string real_path;
    const core::Result resolve_result = resolve_path(user_path, real_path);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    return stat_real_path(sanitize_user_path(user_path), real_path, out_metadata);
}

core::Result FilesystemService::delete_file(const std::string& user_path) const {
    if (!_ready) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not initialized");
    }

    std::string real_path;
    core::Result resolve_result = resolve_path(user_path, real_path);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    FileMetadata metadata;
    const core::Result stat_result = stat_real_path(sanitize_user_path(user_path), real_path, metadata);
    if (!stat_result.ok()) {
        return stat_result;
    }

    if (metadata.is_directory) {
        return core::Result::Failure(core::ErrorCode::ValidationFailed, "Refusing to delete directories");
    }

    if (::unlink(real_path.c_str()) != 0) {
        return core::Result::Failure(
            core::ErrorCode::FileSystemError,
            "Failed to delete file: errno=" + std::to_string(errno));
    }

    return core::Result::Success();
}

core::Result FilesystemService::read_text_file(const std::string& user_path, std::string& out_content) const {
    out_content.clear();

    if (!_ready) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not initialized");
    }

    std::string real_path;
    const core::Result resolve_result = resolve_path(user_path, real_path);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    std::ifstream input(real_path, std::ios::binary);
    if (!input) {
        if (errno == ENOENT) {
            return core::Result::Failure(core::ErrorCode::FileNotFound, "Path not found: " + sanitize_user_path(user_path));
        }

        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to open file for reading");
    }

    out_content.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return core::Result::Success();
}

core::Result FilesystemService::write_text_file_atomic(const std::string& user_path, const std::string& content) const {
    if (!_ready) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not initialized");
    }

    std::string real_path;
    const core::Result resolve_result = resolve_path(user_path, real_path);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    const std::string temp_path = real_path + ".tmp";

    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to open temp file for writing");
        }

        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output.good()) {
            return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed writing temp file");
        }
    }

    if (::rename(temp_path.c_str(), real_path.c_str()) != 0) {
        ::unlink(temp_path.c_str());
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to atomically rename temp file");
    }

    return core::Result::Success();
}

core::Result FilesystemService::resolve_user_path(const std::string& user_path, std::string& out_real_path) const {
    if (!_ready) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not initialized");
    }

    return resolve_path(user_path, out_real_path);
}

core::Result FilesystemService::resolve_path(const std::string& user_path, std::string& out_real_path) const {
    const std::string sanitized = sanitize_user_path(user_path);
    if (!is_allowed_user_path(sanitized)) {
        return core::Result::Failure(core::ErrorCode::PathNotAllowed, "Path is outside allowed roots: " + sanitized);
    }

    out_real_path = std::string(kMountPath) + sanitized;
    return core::Result::Success();
}

bool FilesystemService::is_allowed_user_path(const std::string& user_path) {
    if (user_path.empty()) {
        return false;
    }

    if (user_path.find("..") != std::string::npos) {
        return false;
    }

    return starts_with_root(user_path, "/config") || starts_with_root(user_path, "/images");
}

std::string FilesystemService::sanitize_user_path(const std::string& user_path) {
    if (user_path.empty()) {
        return "/";
    }

    std::string sanitized = user_path;
    if (!sanitized.starts_with('/')) {
        sanitized.insert(sanitized.begin(), '/');
    }

    while (sanitized.size() > 1 && sanitized.back() == '/') {
        sanitized.pop_back();
    }

    return sanitized;
}

std::string FilesystemService::join_user_path(const std::string& base, const std::string& name) {
    if (base == "/") {
        return "/" + name;
    }

    if (base.ends_with('/')) {
        return base + name;
    }

    return base + "/" + name;
}

}  // namespace slidr::services
