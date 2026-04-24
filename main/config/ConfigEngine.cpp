#include "config/ConfigEngine.h"

#include <algorithm>

namespace slidr::config {

bool ConfigEngine::register_schema(ComponentSchema schema) {
    if (schema.type.empty()) {
        return false;
    }
    return _schemas_by_type.emplace(schema.type, std::move(schema)).second;
}

core::Result ConfigEngine::load_desired_config(const JsonDocument& document) {
    _desired_config.clear();
    _pending_restart_components.clear();
    _desired_config.set(document.as<JsonVariantConst>());

    if (!_desired_config["components"].is<JsonArray>()) {
        return core::Result::Failure(core::ErrorCode::ValidationFailed, "Config must contain a 'components' array");
    }

    for (JsonObjectConst component : _desired_config["components"].as<JsonArrayConst>()) {
        std::string validation_error;
        if (!validate_component_entry(component, validation_error)) {
            return core::Result::Failure(core::ErrorCode::ValidationFailed, validation_error);
        }
    }

    return core::Result::Success();
}

core::Result ConfigEngine::load_desired_config_from_json(const std::string& json) {
    JsonDocument parsed;
    const DeserializationError error = deserializeJson(parsed, json);
    if (error) {
        return core::Result::Failure(
            core::ErrorCode::InvalidJson,
            std::string("Failed to parse config JSON: ") + error.c_str());
    }

    return load_desired_config(parsed);
}

core::Result ConfigEngine::apply_targeted_update(
    const std::string& component_id,
    const std::string& field_name,
    const JsonVariantConst& value,
    bool& restart_required) {
    restart_required = false;

    JsonObject component = find_component_by_id(_desired_config, component_id);
    if (component.isNull()) {
        return core::Result::Failure(core::ErrorCode::NotFound, "Component id not found: '" + component_id + "'");
    }

    const std::string component_type = component["type"].as<std::string>();
    const ComponentSchema* schema = find_schema_for_component_type(component_type);
    if (schema == nullptr) {
        return core::Result::Failure(
            core::ErrorCode::ValidationFailed,
            "No schema registered for component type '" + component_type + "'");
    }

    auto field_it = schema->fields.find(field_name);
    if (field_it == schema->fields.end()) {
        return core::Result::Failure(
            core::ErrorCode::ValidationFailed,
            "Field '" + field_name + "' is not defined for component type '" + component_type + "'");
    }

    const FieldSchema& field = field_it->second;
    if (field.validator && !field.validator(value)) {
        return core::Result::Failure(
            core::ErrorCode::ValidationFailed,
            "Field validation failed for '" + field_name + "'");
    }

    JsonObject options;
    if (component["options"].is<JsonObject>()) {
        options = component["options"].as<JsonObject>();
    } else {
        options = component["options"].to<JsonObject>();
    }
    options[field_name] = value;

    if (field.mutability == Mutability::RestartRequired) {
        restart_required = true;
        if (std::find(_pending_restart_components.begin(), _pending_restart_components.end(), component_id)
            == _pending_restart_components.end()) {
            _pending_restart_components.push_back(component_id);
        }

        return core::Result::Failure(
            core::ErrorCode::RestartRequired,
            "Change staged for component '" + component_id + "' and requires restart");
    }

    return core::Result::Success();
}

core::Result ConfigEngine::get_desired_config(JsonDocument& out_document) const {
    out_document.clear();
    out_document.set(_desired_config.as<JsonVariantConst>());
    return core::Result::Success();
}

JsonObject ConfigEngine::find_component_by_id(JsonDocument& document, const std::string& component_id) const {
    if (!document["components"].is<JsonArray>()) {
        return JsonObject();
    }

    JsonArray components = document["components"].as<JsonArray>();
    for (JsonObject component : components) {
        if (!component["id"].is<const char*>()) {
            continue;
        }

        if (component_id == component["id"].as<std::string>()) {
            return component;
        }
    }

    return JsonObject();
}

const ComponentSchema* ConfigEngine::find_schema_for_component_type(const std::string& component_type) const {
    auto it = _schemas_by_type.find(component_type);
    if (it == _schemas_by_type.end()) {
        return nullptr;
    }
    return &it->second;
}

bool ConfigEngine::validate_component_entry(const JsonObjectConst& component, std::string& error_message) const {
    if (!component["id"].is<const char*>()) {
        error_message = "Component entry missing required string field 'id'";
        return false;
    }
    if (!component["type"].is<const char*>()) {
        error_message = "Component entry missing required string field 'type'";
        return false;
    }
    if (!component["options"].is<JsonObject>()) {
        error_message = "Component entry missing required object field 'options'";
        return false;
    }

    return true;
}

}  // namespace slidr::config
