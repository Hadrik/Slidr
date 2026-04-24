#ifndef SLIDR_CONFIG_CONFIG_ENGINE_H
#define SLIDR_CONFIG_CONFIG_ENGINE_H

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <ArduinoJson.h>

#include "config/ConfigSchema.h"
#include "core/Result.h"

namespace slidr::config {

class ConfigEngine {
public:
    bool register_schema(ComponentSchema schema);

    core::Result load_desired_config(const JsonDocument& document);
    core::Result load_desired_config_from_json(const std::string& json);

    core::Result apply_targeted_update(
        const std::string& component_id,
        const std::string& field_name,
        const JsonVariantConst& value,
        bool& restart_required);

    core::Result get_desired_config(JsonDocument& out_document) const;

    const std::vector<std::string>& pending_restart_components() const {
        return _pending_restart_components;
    }

private:
    JsonObject find_component_by_id(JsonDocument& document, const std::string& component_id) const;
    const ComponentSchema* find_schema_for_component_type(const std::string& component_type) const;
    bool validate_component_entry(const JsonObjectConst& component, std::string& error_message) const;

    std::unordered_map<std::string, ComponentSchema> _schemas_by_type {};
    JsonDocument _desired_config {};
    std::vector<std::string> _pending_restart_components {};
};

}  // namespace slidr::config

#endif
