#include "communication/FileTransferManager.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "communication/JsonProtocol.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"

namespace slidr::communication {

namespace {
constexpr const char* kTag = "FileTransferManager";
}

FileTransferManager::FileTransferManager() = default;

FileTransferManager::FileTransferManager(Config config)
    : _config(config) {
}

core::Result FileTransferManager::initialize(services::FilesystemService& filesystem) {
    _filesystem = &filesystem;

    if (_session_mutex == nullptr) {
        _session_mutex = xSemaphoreCreateMutex();
        if (_session_mutex == nullptr) {
            return core::Result::Failure(core::ErrorCode::InternalError, "Failed to create transfer mutex");
        }
    }

    if (_timeout_timer == nullptr) {
        esp_timer_create_args_t args = {
            .callback = &FileTransferManager::on_timeout,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "file_transfer_timeout",
            .skip_unhandled_events = true,
        };

        if (esp_timer_create(&args, &_timeout_timer) != ESP_OK) {
            return core::Result::Failure(core::ErrorCode::InternalError, "Failed to create transfer timeout timer");
        }
    }

    return core::Result::Success();
}

void FileTransferManager::set_event_sink(std::function<void(std::string)> sink) {
    _event_sink = std::move(sink);
}

core::Result FileTransferManager::begin_upload(const std::string& user_path,
                                               std::size_t total_size,
                                               std::size_t requested_chunk_size,
                                               UploadStartInfo& out_info) {
    if (_filesystem == nullptr || !_filesystem->ready()) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not ready");
    }
    if (total_size == 0) {
        return core::Result::Failure(core::ErrorCode::ValidationFailed, "total_size must be greater than zero");
    }

    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (_session.active) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::Busy, "Transfer session already active");
    }

    std::string real_path;
    core::Result resolve_result = resolve_real_path(user_path, real_path);
    if (!resolve_result.ok()) {
        unlock_session();
        return resolve_result;
    }

    std::size_t chunk_size = requested_chunk_size == 0 ? _config.default_chunk_size : requested_chunk_size;
    chunk_size = std::min(chunk_size, _config.max_chunk_size);

    const std::string temp_path = real_path + ".upload";
    std::FILE* file = std::fopen(temp_path.c_str(), "wb");
    if (file == nullptr) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to open temp file for upload");
    }

    _session.active = true;
    _session.direction = Direction::Upload;
    _session.session_id = make_session_id("upload");
    _session.user_path = user_path;
    _session.real_path = real_path;
    _session.temp_path = temp_path;
    _session.total_size = total_size;
    _session.chunk_size = chunk_size;
    _session.bytes_done = 0;
    _session.next_index = 0;
    _session.running_crc32 = 0;
    _session.file = file;

    reset_timeout_locked();

    out_info.session_id = _session.session_id;
    out_info.chunk_size = chunk_size;
    out_info.total_size = total_size;
    out_info.expected_chunks = (total_size + chunk_size - 1) / chunk_size;

    unlock_session();
    return core::Result::Success();
}

core::Result FileTransferManager::accept_upload_chunk(const std::string& session_id,
                                                      std::size_t chunk_index,
                                                      const std::string& base64_data,
                                                      uint32_t chunk_crc32,
                                                      UploadChunkInfo& out_info) {
    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (!_session.active || _session.direction != Direction::Upload || _session.session_id != session_id) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSessionNotFound, "Upload session not found");
    }

    if (chunk_index != _session.next_index) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferOutOfOrder, "Upload chunk out of order");
    }

    std::vector<uint8_t> decoded;
    core::Result decode_result = decode_base64(base64_data, decoded);
    if (!decode_result.ok()) {
        unlock_session();
        return decode_result;
    }

    if (_session.bytes_done + decoded.size() > _session.total_size) {
        clear_session();
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSizeMismatch, "Upload would exceed expected total size");
    }

    const uint32_t computed_crc = esp_crc32_le(0, decoded.data(), decoded.size());
    if (computed_crc != chunk_crc32) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferCrcMismatch, "Chunk CRC mismatch");
    }

    const std::size_t written = std::fwrite(decoded.data(), 1, decoded.size(), _session.file);
    if (written != decoded.size()) {
        clear_session();
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to write upload chunk");
    }

    _session.bytes_done += decoded.size();
    _session.running_crc32 = esp_crc32_le(_session.running_crc32, decoded.data(), decoded.size());
    _session.next_index += 1;

    reset_timeout_locked();

    out_info.bytes_received = _session.bytes_done;
    out_info.total_size = _session.total_size;
    out_info.next_index = _session.next_index;

    unlock_session();
    return core::Result::Success();
}

core::Result FileTransferManager::finish_upload(const std::string& session_id,
                                                uint32_t expected_total_crc32) {
    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (!_session.active || _session.direction != Direction::Upload || _session.session_id != session_id) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSessionNotFound, "Upload session not found");
    }

    if (_session.bytes_done != _session.total_size) {
        clear_session();
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSizeMismatch, "Upload size mismatch");
    }

    if (expected_total_crc32 != 0 && expected_total_crc32 != _session.running_crc32) {
        clear_session();
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferCrcMismatch, "Total CRC mismatch");
    }

    std::fclose(_session.file);
    _session.file = nullptr;

    if (std::rename(_session.temp_path.c_str(), _session.real_path.c_str()) != 0) {
        clear_session();
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to commit upload file");
    }

    clear_session();
    unlock_session();
    return core::Result::Success();
}

core::Result FileTransferManager::cancel_upload(const std::string& session_id) {
    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (!_session.active || _session.direction != Direction::Upload || _session.session_id != session_id) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSessionNotFound, "Upload session not found");
    }

    clear_session();
    unlock_session();
    return core::Result::Success();
}

core::Result FileTransferManager::begin_download(const std::string& user_path,
                                                 std::size_t requested_chunk_size,
                                                 DownloadStartInfo& out_info) {
    if (_filesystem == nullptr || !_filesystem->ready()) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not ready");
    }

    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (_session.active) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::Busy, "Transfer session already active");
    }

    std::string real_path;
    core::Result resolve_result = resolve_real_path(user_path, real_path);
    if (!resolve_result.ok()) {
        unlock_session();
        return resolve_result;
    }

    std::FILE* file = std::fopen(real_path.c_str(), "rb");
    if (file == nullptr) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileNotFound, "File not found for download");
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to seek file");
    }

    const long file_size = std::ftell(file);
    if (file_size < 0) {
        std::fclose(file);
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to determine file size");
    }
    std::rewind(file);

    std::size_t chunk_size = requested_chunk_size == 0 ? _config.default_chunk_size : requested_chunk_size;
    chunk_size = std::min(chunk_size, _config.max_chunk_size);

    std::vector<uint8_t> buffer;
    buffer.resize(chunk_size);

    uint32_t crc = 0;
    std::size_t bytes_left = static_cast<std::size_t>(file_size);
    while (bytes_left > 0) {
        const std::size_t to_read = std::min(chunk_size, bytes_left);
        const std::size_t read_bytes = std::fread(buffer.data(), 1, to_read, file);
        if (read_bytes != to_read) {
            std::fclose(file);
            unlock_session();
            return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to read file for CRC");
        }
        crc = esp_crc32_le(crc, buffer.data(), read_bytes);
        bytes_left -= read_bytes;
    }

    std::rewind(file);

    _session.active = true;
    _session.direction = Direction::Download;
    _session.session_id = make_session_id("download");
    _session.user_path = user_path;
    _session.real_path = real_path;
    _session.temp_path.clear();
    _session.total_size = static_cast<std::size_t>(file_size);
    _session.chunk_size = chunk_size;
    _session.bytes_done = 0;
    _session.next_index = 0;
    _session.running_crc32 = 0;
    _session.file = file;

    reset_timeout_locked();

    out_info.session_id = _session.session_id;
    out_info.chunk_size = chunk_size;
    out_info.total_size = _session.total_size;
    out_info.total_chunks = (_session.total_size + chunk_size - 1) / chunk_size;
    out_info.total_crc32 = crc;

    unlock_session();
    return core::Result::Success();
}

core::Result FileTransferManager::get_download_chunk(const std::string& session_id,
                                                     std::size_t chunk_index,
                                                     DownloadChunkInfo& out_info) {
    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (!_session.active || _session.direction != Direction::Download || _session.session_id != session_id) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSessionNotFound, "Download session not found");
    }

    if (chunk_index != _session.next_index) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferOutOfOrder, "Download chunk out of order");
    }

    const std::size_t offset = chunk_index * _session.chunk_size;
    if (offset >= _session.total_size) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSizeMismatch, "Chunk index exceeds file size");
    }

    if (std::fseek(_session.file, static_cast<long>(offset), SEEK_SET) != 0) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to seek file for download");
    }

    const std::size_t remaining = _session.total_size - offset;
    const std::size_t to_read = std::min(_session.chunk_size, remaining);

    std::vector<uint8_t> buffer;
    buffer.resize(to_read);

    const std::size_t read_bytes = std::fread(buffer.data(), 1, to_read, _session.file);
    if (read_bytes != to_read) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to read download chunk");
    }

    out_info.chunk_crc32 = esp_crc32_le(0, buffer.data(), buffer.size());
    core::Result encode_result = encode_base64(buffer.data(), buffer.size(), out_info.chunk_base64);
    if (!encode_result.ok()) {
        unlock_session();
        return encode_result;
    }

    out_info.chunk_size = buffer.size();
    out_info.chunk_index = chunk_index;
    out_info.is_last = (offset + buffer.size()) >= _session.total_size;

    _session.bytes_done = offset + buffer.size();
    _session.next_index += 1;

    reset_timeout_locked();

    unlock_session();
    return core::Result::Success();
}

core::Result FileTransferManager::finish_download(const std::string& session_id) {
    core::Result lock_result = lock_session(pdMS_TO_TICKS(50));
    if (!lock_result.ok()) {
        return lock_result;
    }

    if (!_session.active || _session.direction != Direction::Download || _session.session_id != session_id) {
        unlock_session();
        return core::Result::Failure(core::ErrorCode::TransferSessionNotFound, "Download session not found");
    }

    clear_session();
    unlock_session();
    return core::Result::Success();
}

void FileTransferManager::on_timeout(void* arg) {
    auto* manager = static_cast<FileTransferManager*>(arg);
    if (manager != nullptr) {
        manager->handle_timeout();
    }
}

void FileTransferManager::handle_timeout() {
    if (_session_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(_session_mutex, 0) != pdTRUE) {
        if (_timeout_timer != nullptr) {
            esp_timer_start_once(_timeout_timer, 100 * 1000ULL);
        }
        return;
    }

    if (!_session.active) {
        xSemaphoreGive(_session_mutex);
        return;
    }

    Session snapshot = _session;
    clear_session();
    xSemaphoreGive(_session_mutex);

    (void) send_timeout_event(snapshot);
}

core::Result FileTransferManager::lock_session(TickType_t ticks_to_wait) {
    if (_session_mutex == nullptr) {
        return core::Result::Failure(core::ErrorCode::InternalError, "Transfer mutex unavailable");
    }

    if (xSemaphoreTake(_session_mutex, ticks_to_wait) != pdTRUE) {
        return core::Result::Failure(core::ErrorCode::Busy, "Transfer session busy");
    }

    return core::Result::Success();
}

void FileTransferManager::unlock_session() {
    if (_session_mutex != nullptr) {
        xSemaphoreGive(_session_mutex);
    }
}

void FileTransferManager::clear_session() {
    stop_timeout_locked();

    if (_session.file != nullptr) {
        std::fclose(_session.file);
        _session.file = nullptr;
    }

    if (_session.direction == Direction::Upload && !_session.temp_path.empty()) {
        std::remove(_session.temp_path.c_str());
    }

    _session = Session{};
}

void FileTransferManager::reset_timeout_locked() {
    if (_timeout_timer == nullptr) {
        return;
    }

    esp_timer_stop(_timeout_timer);
    esp_timer_start_once(_timeout_timer, static_cast<uint64_t>(_config.timeout_ms) * 1000ULL);
}

void FileTransferManager::stop_timeout_locked() {
    if (_timeout_timer != nullptr) {
        esp_timer_stop(_timeout_timer);
    }
}

std::string FileTransferManager::make_session_id(const char* prefix) const {
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
    return std::string(prefix) + "_" + std::to_string(now_ms);
}

core::Result FileTransferManager::resolve_real_path(const std::string& user_path, std::string& out_real_path) const {
    if (_filesystem == nullptr) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not available");
    }

    return _filesystem->resolve_user_path(user_path, out_real_path);
}

core::Result FileTransferManager::decode_base64(const std::string& input, std::vector<uint8_t>& output) const {
    if (input.empty()) {
        output.clear();
        return core::Result::Success();
    }

    const std::size_t max_size = (input.size() * 3) / 4 + 4;
    output.resize(max_size);

    std::size_t decoded_len = 0;
    const int result = mbedtls_base64_decode(output.data(), output.size(), &decoded_len,
                                             reinterpret_cast<const unsigned char*>(input.data()), input.size());
    if (result != 0) {
        output.clear();
        return core::Result::Failure(core::ErrorCode::TransferDecodeFailed, "Base64 decode failed");
    }

    output.resize(decoded_len);
    return core::Result::Success();
}

core::Result FileTransferManager::encode_base64(const uint8_t* data, std::size_t length, std::string& output) const {
    if (length == 0) {
        output.clear();
        return core::Result::Success();
    }

    const std::size_t max_size = ((length + 2) / 3) * 4 + 1;
    output.assign(max_size, '\0');

    std::size_t encoded_len = 0;
    const int result = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(output.data()),
                                             output.size(),
                                             &encoded_len,
                                             data,
                                             length);
    if (result != 0) {
        output.clear();
        return core::Result::Failure(core::ErrorCode::InternalError, "Base64 encode failed");
    }

    output.resize(encoded_len);
    return core::Result::Success();
}

core::Result FileTransferManager::send_timeout_event(const Session& snapshot) {
    if (!_event_sink) {
        return core::Result::Success();
    }

    JsonDocument payload;
    payload["session_id"] = snapshot.session_id;
    payload["path"] = snapshot.user_path;
    payload["bytes_transferred"] = static_cast<uint32_t>(snapshot.bytes_done);
    payload["total_size"] = static_cast<uint32_t>(snapshot.total_size);
    payload["direction"] = snapshot.direction == Direction::Upload ? "upload" : "download";
    payload["reason"] = "timeout";

    JsonDocument envelope;
    envelope["protocol_version"] = "1.0";
    envelope["kind"] = "file.transfer.timeout";
    envelope["message_id"] = make_session_id("event");
    envelope["timestamp_ms"] = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
    envelope["status"] = "error";
    envelope["error_code"] = core::ToString(core::ErrorCode::Timeout);
    envelope["payload"] = payload.as<JsonVariantConst>();

    const std::string line = JsonProtocol::serialize_line(envelope);
    _event_sink(line);

    return core::Result::Success();
}

}  // namespace slidr::communication
