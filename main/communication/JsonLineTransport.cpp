#include "communication/JsonLineTransport.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"

namespace slidr::communication {

namespace {
constexpr const char* kTag = "JsonLineTransport";
}

JsonLineTransport::JsonLineTransport(Config config)
    : _config(config) {
}

core::Result JsonLineTransport::begin() {
    if (_initialized) {
        return core::Result::Success();
    }

    setvbuf(stdin, nullptr, _IONBF, 0);

    const int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (stdin_flags >= 0) {
        if (fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) < 0) {
            ESP_LOGW(kTag, "Failed to set stdin non-blocking mode");
        }
    }

    const int stdout_flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (stdout_flags >= 0) {
        if (fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK) < 0) {
            ESP_LOGW(kTag, "Failed to set stdout non-blocking mode");
        }
    }

    ESP_LOGI(kTag, "JsonLineTransport initialized with config: max_line_length=%zu, max_incoming_queue=%zu, max_outgoing_queue=%zu, max_rx_bytes_per_poll=%zu, partial_line_timeout_ms=%u",
        _config.max_line_length,
        _config.max_incoming_queue,
        _config.max_outgoing_queue,
        _config.max_rx_bytes_per_poll,
        _config.partial_line_timeout_ms);
    _initialized = true;
    return core::Result::Success();
}

void JsonLineTransport::poll() {
    if (!_initialized) {
        return;
    }

    drain_rx();
    drain_tx();
}

bool JsonLineTransport::try_pop_incoming_line(std::string& out_line) {
    if (_incoming_lines.empty()) {
        return false;
    }

    out_line = std::move(_incoming_lines.front());
    _incoming_lines.pop_front();
    return true;
}

core::Result JsonLineTransport::queue_outgoing_line(std::string line) {
    if (line.empty()) {
        return core::Result::Failure(core::ErrorCode::ValidationFailed, "Cannot queue empty outgoing line");
    }

    if (line.back() != '\n') {
        line.push_back('\n');
    }

    if (_outgoing_lines.size() >= _config.max_outgoing_queue) {
        return core::Result::Failure(core::ErrorCode::QueueFull, "Outgoing queue is full");
    }

    _outgoing_lines.push_back(std::move(line));
    return core::Result::Success();
}

void JsonLineTransport::drain_rx() {
    std::size_t total_read = 0;
    char chunk[128];

    while (total_read < _config.max_rx_bytes_per_poll) {
        const std::size_t request_size = std::min(sizeof(chunk), _config.max_rx_bytes_per_poll - total_read);
        const ssize_t read_bytes = ::read(STDIN_FILENO, chunk, request_size);

        if (read_bytes <= 0) {
            if (read_bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(kTag, "stdin read error: errno=%d", errno);
            }
            break;
        }

        total_read += static_cast<std::size_t>(read_bytes);

        for (ssize_t i = 0; i < read_bytes; ++i) {
            const char c = chunk[i];

            if (_rx_buffer.empty()) {
                _rx_started_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
            }

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                if (!_rx_buffer.empty()) {
                    if (_incoming_lines.size() < _config.max_incoming_queue) {
                        ESP_LOGI(kTag, "Received line: '%s'", _rx_buffer.c_str());
                        _incoming_lines.push_back(_rx_buffer);
                    } else {
                        ESP_LOGW(kTag, "Incoming queue full, dropping line");
                    }
                }
                _rx_buffer.clear();
                _rx_started_ms = 0;
                continue;
            }

            if (_rx_buffer.size() >= _config.max_line_length) {
                ESP_LOGW(kTag, "Incoming line exceeded max length, dropping partial line");
                _rx_buffer.clear();
                _rx_started_ms = 0;
                continue;
            }

            _rx_buffer.push_back(c);
        }
    }

    if (!_rx_buffer.empty()) {
        const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        if ((now_ms - _rx_started_ms) > _config.partial_line_timeout_ms) {
            ESP_LOGW(kTag, "Partial input line timed out, dropping partial line");
            _rx_buffer.clear();
            _rx_started_ms = 0;
        }
    }
}

void JsonLineTransport::drain_tx() {
    while (true) {
        if (_active_tx_line.empty()) {
            if (_outgoing_lines.empty()) {
                return;
            }
            _active_tx_line = std::move(_outgoing_lines.front());
            _outgoing_lines.pop_front();
            _active_tx_offset = 0;
        }

        const char* write_ptr = _active_tx_line.data() + _active_tx_offset;
        const std::size_t bytes_left = _active_tx_line.size() - _active_tx_offset;
        const ssize_t written = ::write(STDOUT_FILENO, write_ptr, bytes_left);

        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            ESP_LOGW(kTag, "stdout write error: errno=%d, dropping line", errno);
            _active_tx_line.clear();
            _active_tx_offset = 0;
            continue;
        }

        _active_tx_offset += static_cast<std::size_t>(written);
        if (_active_tx_offset >= _active_tx_line.size()) {
            _active_tx_line.clear();
            _active_tx_offset = 0;
        }
    }
}

}  // namespace slidr::communication
