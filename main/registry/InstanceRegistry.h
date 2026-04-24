#ifndef SLIDR_REGISTRY_INSTANCE_REGISTRY_H
#define SLIDR_REGISTRY_INSTANCE_REGISTRY_H

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace slidr::registry {

template <typename TInterface>
class InstanceRegistry {
public:
    bool register_instance(const std::string& id, std::shared_ptr<TInterface> instance) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _instances.emplace(id, std::move(instance)).second;
    }

    std::shared_ptr<TInterface> find(const std::string& id) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _instances.find(id);
        if (it == _instances.end()) {
            return nullptr;
        }
        return it->second;
    }

    bool remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _instances.erase(id) > 0;
    }

    std::vector<std::shared_ptr<TInterface>> all() const {
        std::lock_guard<std::mutex> lock(_mutex);

        std::vector<std::shared_ptr<TInterface>> result;
        result.reserve(_instances.size());
        for (const auto& [id, instance] : _instances) {
            (void) id;
            result.push_back(instance);
        }
        return result;
    }

private:
    mutable std::mutex _mutex;
    std::unordered_map<std::string, std::shared_ptr<TInterface>> _instances {};
};

}  // namespace slidr::registry

#endif
