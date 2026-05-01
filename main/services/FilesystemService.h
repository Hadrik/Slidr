#ifndef SLIDR_SERVICES_FILESYSTEM_SERVICE_H
#define SLIDR_SERVICES_FILESYSTEM_SERVICE_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Result.h"

namespace slidr::services {

struct FileMetadata {
    std::string path {};
    std::size_t size = 0;
    uint64_t modified_ms = 0;
    bool is_directory = false;
};

class FilesystemService {
public:
    core::Result initialize();

    [[nodiscard]] bool ready() const {
        return _ready;
    }

    core::Result list(const std::string& user_path, std::vector<FileMetadata>& out_entries) const;
    core::Result stat(const std::string& user_path, FileMetadata& out_metadata) const;
    core::Result delete_file(const std::string& user_path) const;

    core::Result read_text_file(const std::string& user_path, std::string& out_content) const;
    core::Result write_text_file_atomic(const std::string& user_path, const std::string& content) const;

    core::Result resolve_user_path(const std::string& user_path, std::string& out_real_path) const;

private:
    core::Result resolve_path(const std::string& user_path, std::string& out_real_path) const;

    static bool is_allowed_user_path(const std::string& user_path);
    static std::string sanitize_user_path(const std::string& user_path);
    static std::string join_user_path(const std::string& base, const std::string& name);

    bool _ready = false;

    static constexpr const char* kMountPath = "/littlefs";
    static constexpr const char* kPartitionLabel = "storage";
};

}  // namespace slidr::services

#endif
