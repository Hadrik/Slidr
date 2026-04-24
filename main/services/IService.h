#ifndef SLIDR_SERVICES_ISERVICE_H
#define SLIDR_SERVICES_ISERVICE_H

#pragma once

#include <string>

#include <ArduinoJson.h>

#include "core/Result.h"

namespace slidr::services {

class IService {
public:
    virtual ~IService() = default;

    virtual const std::string& id() const = 0;
    virtual const std::string& type() const = 0;

    virtual core::Result start() = 0;
    virtual core::Result stop() = 0;

    virtual core::Result apply_runtime_option(const std::string& field_name, const JsonVariantConst& value) = 0;
};

}  // namespace slidr::services

#endif
