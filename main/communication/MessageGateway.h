#ifndef SLIDR_COMMUNICATION_MESSAGE_GATEWAY_H
#define SLIDR_COMMUNICATION_MESSAGE_GATEWAY_H

#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <ArduinoJson.h>

#include "core/Result.h"

namespace slidr::communication {

class MessageGateway {
public:
    using Handler = std::function<core::Result(const JsonObjectConst& payload, JsonDocument& response_payload)>;

    bool register_handler(const std::string& kind, Handler handler);
    core::Result handle_incoming_line(const std::string& line, std::string& out_line) const;

private:
    std::unordered_map<std::string, Handler> _handlers {};
};

}  // namespace slidr::communication

#endif
