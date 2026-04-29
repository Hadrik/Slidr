#include "communication/MessageGateway.h"

#include "communication/JsonProtocol.h"

namespace slidr::communication {

bool MessageGateway::register_handler(const std::string& kind, Handler handler) {
    return _handlers.emplace(kind, std::move(handler)).second;
}

core::Result MessageGateway::handle_incoming_line(const std::string& line, std::string& out_line) const {
    JsonDocument request;
    core::Result result = JsonProtocol::parse_line(line, request);

    std::string message_id;
    JsonDocument response_payload;

    if (result.ok()) {
        JsonObjectConst envelope = request.as<JsonObjectConst>();
        result = JsonProtocol::validate_envelope(envelope);

        if (result.ok()) {
            message_id = envelope["message_id"].as<std::string>();
            const std::string kind = envelope["kind"].as<std::string>();

            auto it = _handlers.find(kind);
            if (it == _handlers.end()) {
                result = core::Result::Failure(
                    core::ErrorCode::UnknownCommand,
                    "No handler registered for message kind '" + kind + "'");
            } else {
                JsonObjectConst payload = envelope["payload"].is<JsonObjectConst>()
                    ? envelope["payload"].as<JsonObjectConst>()
                    : JsonObjectConst();
                result = it->second(payload, response_payload);
            }
        }
    }

    JsonDocument response_envelope;
    JsonProtocol::build_response_envelope(message_id, result, response_payload, response_envelope);
    out_line = JsonProtocol::serialize_line(response_envelope);

    return result;
}

}  // namespace slidr::communication
