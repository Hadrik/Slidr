#ifndef SLIDR_PARTS_IPART_H
#define SLIDR_PARTS_IPART_H

#pragma once

#include <string>

#include <ArduinoJson.h>

#include "core/Result.h"

namespace slidr::parts {

class IPart {
public:
    virtual ~IPart() = default;

    virtual const std::string& id() const = 0;
    virtual const std::string& type() const = 0;

    virtual core::Result start() = 0;
    virtual core::Result stop() = 0;

    virtual core::Result apply_runtime_option(const std::string& field_name, const JsonVariantConst& value) = 0;
};

}  // namespace slidr::parts

#endif
