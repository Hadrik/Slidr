#ifndef SLIDR_CONFIG_CONFIG_SCHEMA_H
#define SLIDR_CONFIG_CONFIG_SCHEMA_H

#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <ArduinoJson.h>

namespace slidr::config {

enum class Mutability {
    RuntimeMutable,
    RestartRequired,
};

using JsonValidator = std::function<bool(const JsonVariantConst&)>;

struct FieldSchema {
    std::string name;
    Mutability mutability = Mutability::RuntimeMutable;
    JsonValidator validator {};
};

struct ComponentSchema {
    std::string type;
    std::unordered_map<std::string, FieldSchema> fields {};
};

namespace validators {

inline JsonValidator any() {
    return [](const JsonVariantConst&) {
        return true;
    };
}

inline JsonValidator boolean() {
    return [](const JsonVariantConst& value) {
        return value.is<bool>();
    };
}

inline JsonValidator non_empty_string() {
    return [](const JsonVariantConst& value) {
        return value.is<const char*>() && std::string(value.as<const char*>()).empty() == false;
    };
}

inline JsonValidator integer() {
    return [](const JsonVariantConst& value) {
        return value.is<int>() || value.is<long>() || value.is<unsigned>() || value.is<unsigned long>();
    };
}

inline JsonValidator in_range_int(int minimum, int maximum) {
    return [minimum, maximum](const JsonVariantConst& value) {
        if (!(value.is<int>() || value.is<long>() || value.is<unsigned>() || value.is<unsigned long>())) {
            return false;
        }

        const int parsed = value.as<int>();
        return parsed >= minimum && parsed <= maximum;
    };
}

}  // namespace validators

}  // namespace slidr::config

#endif
