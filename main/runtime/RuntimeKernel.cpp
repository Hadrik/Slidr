#include "runtime/RuntimeKernel.h"

#include "config/ConfigSchema.h"

namespace slidr::runtime {

void RuntimeKernel::initialize() {
    if (_initialized) {
        return;
    }

    register_default_schemas();
    register_default_handlers();

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
}

}  // namespace slidr::runtime
