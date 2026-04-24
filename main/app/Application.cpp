#include "app/Application.h"

#include "esp_log.h"

namespace slidr::app {

namespace {
constexpr const char* kTag = "Application";
}

void Application::begin() {
    _kernel.initialize();
    ESP_LOGI(kTag, "Runtime kernel initialized");

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
    // Runtime loop extension point.
}

core::Result Application::process_line(const std::string& line, std::string& out_line) {
    return _kernel.handle_line(line, out_line);
}

}  // namespace slidr::app
