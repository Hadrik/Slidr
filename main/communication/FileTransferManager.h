#ifndef SLIDR_COMMUNICATION_FILE_TRANSFER_MANAGER_H
#define SLIDR_COMMUNICATION_FILE_TRANSFER_MANAGER_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/Result.h"
#include "services/FilesystemService.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace slidr::communication {

class FileTransferManager {
public:
    struct Config {
        uint32_t timeout_ms = 5000;
        std::size_t default_chunk_size = 768;
        std::size_t max_chunk_size = 2048;
    };

    struct UploadStartInfo {
        std::string session_id;
        std::size_t chunk_size = 0;
        std::size_t total_size = 0;
        std::size_t expected_chunks = 0;
    };

    struct UploadChunkInfo {
        std::size_t bytes_received = 0;
        std::size_t total_size = 0;
        std::size_t next_index = 0;
    };

    struct DownloadStartInfo {
        std::string session_id;
        std::size_t chunk_size = 0;
        std::size_t total_size = 0;
        std::size_t total_chunks = 0;
        uint32_t total_crc32 = 0;
    };

    struct DownloadChunkInfo {
        std::string chunk_base64;
        std::size_t chunk_size = 0;
        std::size_t chunk_index = 0;
        bool is_last = false;
        uint32_t chunk_crc32 = 0;
    };

    FileTransferManager();
    explicit FileTransferManager(Config config);

    core::Result initialize(services::FilesystemService& filesystem);
    void set_event_sink(std::function<void(std::string)> sink);

    core::Result begin_upload(const std::string& user_path,
                              std::size_t total_size,
                              std::size_t requested_chunk_size,
                              UploadStartInfo& out_info);

    core::Result accept_upload_chunk(const std::string& session_id,
                                     std::size_t chunk_index,
                                     const std::string& base64_data,
                                     uint32_t chunk_crc32,
                                     UploadChunkInfo& out_info);

    core::Result finish_upload(const std::string& session_id,
                               uint32_t expected_total_crc32);

    core::Result cancel_upload(const std::string& session_id);

    core::Result begin_download(const std::string& user_path,
                                std::size_t requested_chunk_size,
                                DownloadStartInfo& out_info);

    core::Result get_download_chunk(const std::string& session_id,
                                    std::size_t chunk_index,
                                    DownloadChunkInfo& out_info);

    core::Result finish_download(const std::string& session_id);

private:
    enum class Direction {
        None,
        Upload,
        Download
    };

    struct Session {
        Direction direction = Direction::None;
        std::string session_id;
        std::string user_path;
        std::string real_path;
        std::string temp_path;
        std::size_t total_size = 0;
        std::size_t chunk_size = 0;
        std::size_t bytes_done = 0;
        std::size_t next_index = 0;
        uint32_t running_crc32 = 0;
        bool active = false;
        std::FILE* file = nullptr;
    };

    static void on_timeout(void* arg);
    void handle_timeout();

    core::Result lock_session(TickType_t ticks_to_wait);
    void unlock_session();
    void clear_session();

    void reset_timeout_locked();
    void stop_timeout_locked();

    std::string make_session_id(const char* prefix) const;
    core::Result resolve_real_path(const std::string& user_path, std::string& out_real_path) const;

    core::Result decode_base64(const std::string& input, std::vector<uint8_t>& output) const;
    core::Result encode_base64(const uint8_t* data, std::size_t length, std::string& output) const;

    core::Result send_timeout_event(const Session& snapshot);

    Config _config {};
    services::FilesystemService* _filesystem = nullptr;
    Session _session {};

    SemaphoreHandle_t _session_mutex = nullptr;
    esp_timer_handle_t _timeout_timer = nullptr;

    std::function<void(std::string)> _event_sink;
};

}  // namespace slidr::communication

#endif
