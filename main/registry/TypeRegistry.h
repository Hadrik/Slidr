#ifndef SLIDR_REGISTRY_TYPE_REGISTRY_H
#define SLIDR_REGISTRY_TYPE_REGISTRY_H

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <ArduinoJson.h>

namespace slidr::registry {

template <typename TInterface>
class TypeRegistry {
public:
    using Factory = std::function<std::shared_ptr<TInterface>(const std::string& id, const JsonObjectConst& options)>;

    bool register_type(const std::string& type_name, Factory factory) {
        return _factories.emplace(type_name, std::move(factory)).second;
    }

    std::shared_ptr<TInterface> create(const std::string& type_name, const std::string& id, const JsonObjectConst& options) const {
        auto it = _factories.find(type_name);
        if (it == _factories.end()) {
            return nullptr;
        }
        return it->second(id, options);
    }

    [[nodiscard]] bool has_type(const std::string& type_name) const {
        return _factories.find(type_name) != _factories.end();
    }

private:
    std::unordered_map<std::string, Factory> _factories {};
};

}  // namespace slidr::registry

#endif
