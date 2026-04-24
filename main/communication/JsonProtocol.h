#ifndef SLIDR_COMMUNICATION_JSON_PROTOCOL_H
#define SLIDR_COMMUNICATION_JSON_PROTOCOL_H

#pragma once

#include <string>

#include <ArduinoJson.h>

#include "core/Result.h"

namespace slidr::communication {

class JsonProtocol {
public:
    static core::Result parse_line(const std::string& line, JsonDocument& out_document);
    static core::Result validate_envelope(const JsonObjectConst& envelope);

    static void build_response_envelope(
        const std::string& message_id,
        const core::Result& result,
        const JsonDocument& payload,
        JsonDocument& out_envelope);

    static std::string serialize_line(const JsonDocument& document);
};

}  // namespace slidr::communication

#endif
