#include "app/Application.h"

#include <utility>

#include "esp_log.h"

namespace slidr::app {

namespace {
constexpr const char* kTag = "Application";
}

void Application::begin() {
    _kernel.initialize();
    ESP_LOGI(kTag, "Runtime kernel initialized");

    const core::Result transport_result = _transport.begin();
    if (!transport_result.ok()) {
        ESP_LOGW(kTag, "Transport init failed: %s", transport_result.message.c_str());
    }

    std::string response;
    const std::string ping_request =
        "{\"protocol_version\":\"1.0\",\"message_id\":\"boot_ping_1\",\"kind\":\"ping\",\"payload\":{}}\n";

    const core::Result result = _kernel.handle_line(ping_request, response);
    if (result.ok()) {
        ESP_LOGI(kTag, "Self-test ping succeeded: %s", response.c_str());
    } else {
        ESP_LOGW(kTag, "Self-test ping failed: %s", result.message.c_str());
    }
}

void Application::tick() {
    _transport.poll();

    constexpr int kMaxMessagesPerTick = 4;
    int processed = 0;

    while (processed < kMaxMessagesPerTick) {
        std::string incoming_line;
        if (!_transport.try_pop_incoming_line(incoming_line)) {
            break;
        }

        std::string response_line;
        const core::Result result = _kernel.handle_line(incoming_line, response_line);
        if (!result.ok()) {
            ESP_LOGW(kTag, "Request handling failed: %s", result.message.c_str());
        }

        if (!response_line.empty()) {
            const core::Result queue_result = _transport.queue_outgoing_line(std::move(response_line));
            if (!queue_result.ok()) {
                ESP_LOGW(kTag, "Response queueing failed: %s", queue_result.message.c_str());
            }
        }

        ++processed;
    }

    _transport.poll();
}

core::Result Application::process_line(const std::string& line, std::string& out_line) {
    return _kernel.handle_line(line, out_line);
}

}  // namespace slidr::app
