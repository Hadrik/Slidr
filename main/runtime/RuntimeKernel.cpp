#include "runtime/RuntimeKernel.h"

#include <vector>

#include "config/ConfigSchema.h"
#include "esp_log.h"

namespace {
constexpr const char* kTag = "RuntimeKernel";
}

namespace slidr::runtime {

void RuntimeKernel::initialize() {
    if (_initialized) {
        return;
    }

    register_default_schemas();
    register_default_handlers();

    const core::Result fs_result = _filesystem.initialize();
    if (!fs_result.ok()) {
        ESP_LOGW(kTag, "Filesystem init failed: %s", fs_result.message.c_str());
    }

    static constexpr const char* kBootstrapConfig = R"json(
{
  "components": [
    {
      "id": "slider_0",
      "type": "slider",
      "options": {
        "pin": 1,
        "min_value": 0,
        "max_value": 100,
        "deadzone": 2,
        "adc_bits": 12
      }
    },
    {
      "id": "led_0",
      "type": "led",
      "options": {
        "pin": 15
      }
    }
  ]
}
)json";

    (void) _config_engine.load_desired_config_from_json(kBootstrapConfig);
    _initialized = true;
}

core::Result RuntimeKernel::handle_line(const std::string& line, std::string& out_line) {
    return _gateway.handle_incoming_line(line, out_line);
}

void RuntimeKernel::register_default_schemas() {
    config::ComponentSchema slider_schema {
        .type = "slider",
        .fields = {
            {"pin", config::FieldSchema {
                .name = "pin",
                .mutability = config::Mutability::RestartRequired,
                .validator = config::validators::in_range_int(0, 48),
            }},
            {"min_value", config::FieldSchema {
                .name = "min_value",
                .mutability = config::Mutability::RuntimeMutable,
                .validator = config::validators::in_range_int(0, 100),
            }},
            {"max_value", config::FieldSchema {
                .name = "max_value",
                .mutability = config::Mutability::RuntimeMutable,
                .validator = config::validators::in_range_int(0, 100),
            }},
            {"deadzone", config::FieldSchema {
                .name = "deadzone",
                .mutability = config::Mutability::RuntimeMutable,
                .validator = config::validators::in_range_int(0, 50),
            }},
            {"adc_bits", config::FieldSchema {
                .name = "adc_bits",
                .mutability = config::Mutability::RestartRequired,
                .validator = config::validators::in_range_int(9, 13),
            }},
        }
    };
    (void) _config_engine.register_schema(std::move(slider_schema));

    config::ComponentSchema led_schema {
        .type = "led",
        .fields = {
            {"pin", config::FieldSchema {
                .name = "pin",
                .mutability = config::Mutability::RestartRequired,
                .validator = config::validators::in_range_int(0, 48),
            }},
        }
    };
    (void) _config_engine.register_schema(std::move(led_schema));
}

void RuntimeKernel::register_default_handlers() {
    _gateway.register_handler("ping", [](const JsonObjectConst&, JsonDocument& response_payload) {
        response_payload["pong"] = true;
        return core::Result::Success();
    });

    _gateway.register_handler("heartbeat", [this](const JsonObjectConst&, JsonDocument& response_payload) {
        response_payload["alive"] = true;
        response_payload["filesystem_ready"] = _filesystem.ready();

        JsonArray pending = response_payload["pending_restart_components"].to<JsonArray>();
        for (const auto& id : _config_engine.pending_restart_components()) {
            pending.add(id);
        }

        return core::Result::Success();
    });

    _gateway.register_handler("config.get", [this](const JsonObjectConst&, JsonDocument& response_payload) {
        JsonDocument desired;
        core::Result result = _config_engine.get_desired_config(desired);
        if (!result.ok()) {
            return result;
        }

        response_payload["document"] = desired.as<JsonVariantConst>();
        return core::Result::Success();
    });

    _gateway.register_handler("config.apply_full", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["document"].is<JsonObject>()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.apply_full requires object field 'document'");
        }

        JsonDocument document;
        document.set(payload["document"].as<JsonObjectConst>());

        core::Result result = _config_engine.load_desired_config(document);
        if (result.ok()) {
            response_payload["applied"] = true;
        }
        return result;
    });

    _gateway.register_handler("config.update_field", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["component_id"].is<const char*>()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.update_field requires string field 'component_id'");
        }
        if (!payload["field"].is<const char*>()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.update_field requires string field 'field'");
        }
        if (payload["value"].isNull()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.update_field requires field 'value'");
        }

        const std::string component_id = payload["component_id"].as<std::string>();
        const std::string field_name = payload["field"].as<std::string>();

        bool restart_required = false;
        core::Result result = _config_engine.apply_targeted_update(
            component_id,
            field_name,
            payload["value"],
            restart_required);

        response_payload["component_id"] = component_id;
        response_payload["field"] = field_name;
        response_payload["restart_required"] = restart_required;

        if (restart_required) {
            JsonArray pending = response_payload["pending_restart_components"].to<JsonArray>();
            for (const auto& id : _config_engine.pending_restart_components()) {
                pending.add(id);
            }
        }

        return result;
    });

    _gateway.register_handler("file.list", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        const std::string path = payload["path"] | "/";

        std::vector<services::FileMetadata> entries;
        const core::Result result = _filesystem.list(path, entries);
        if (!result.ok()) {
            return result;
        }

        response_payload["path"] = path;
        response_payload["count"] = static_cast<uint32_t>(entries.size());

        JsonArray json_entries = response_payload["entries"].to<JsonArray>();
        for (const auto& entry : entries) {
            JsonObject item = json_entries.add<JsonObject>();
            item["path"] = entry.path;
            item["size"] = static_cast<uint32_t>(entry.size);
            item["modified_ms"] = entry.modified_ms;
            item["is_directory"] = entry.is_directory;
        }

        return core::Result::Success();
    });

    _gateway.register_handler("file.stat", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["path"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.stat requires string field 'path'");
        }

        const std::string path = payload["path"].as<std::string>();

        services::FileMetadata metadata;
        const core::Result result = _filesystem.stat(path, metadata);
        if (!result.ok()) {
            return result;
        }

        response_payload["path"] = metadata.path;
        response_payload["size"] = static_cast<uint32_t>(metadata.size);
        response_payload["modified_ms"] = metadata.modified_ms;
        response_payload["is_directory"] = metadata.is_directory;
        return core::Result::Success();
    });

    _gateway.register_handler("file.delete", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["path"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.delete requires string field 'path'");
        }

        const std::string path = payload["path"].as<std::string>();
        const core::Result result = _filesystem.delete_file(path);
        if (!result.ok()) {
            return result;
        }

        response_payload["path"] = path;
        response_payload["deleted"] = true;
        return core::Result::Success();
    });
}

}  // namespace slidr::runtime
