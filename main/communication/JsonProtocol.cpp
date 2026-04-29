#include "communication/JsonProtocol.h"

#include "esp_timer.h"
#include "esp_log.h"

namespace slidr::communication {

core::Result JsonProtocol::parse_line(const std::string& line, JsonDocument& out_document) {
    out_document.clear();

    const DeserializationError error = deserializeJson(out_document, line);
    if (error) {
        ESP_LOGW("JsonProtocol", "JSON parse error. Line: '%s', Error: %s", line.c_str(), error.c_str());
        return core::Result::Failure(
            core::ErrorCode::InvalidJson,
            std::string("Failed to parse JSON line: ") + error.c_str());
    }

    if (!out_document.is<JsonObject>()) {
        return core::Result::Failure(
            core::ErrorCode::InvalidJson,
            "Top-level JSON value must be an object");
    }

    return core::Result::Success();
}

core::Result JsonProtocol::validate_envelope(const JsonObjectConst& envelope) {
    if (!envelope["protocol_version"].is<const char*>()) {
        return core::Result::Failure(core::ErrorCode::MissingField, "Missing required string field 'protocol_version'");
    }
    if (!envelope["message_id"].is<const char*>()) {
        return core::Result::Failure(core::ErrorCode::MissingField, "Missing required string field 'message_id'");
    }
    if (!envelope["kind"].is<const char*>()) {
        return core::Result::Failure(core::ErrorCode::MissingField, "Missing required string field 'kind'");
    }

    if (!envelope["payload"].is<JsonObjectConst>() && !envelope["payload"].isNull()) {
        return core::Result::Failure(core::ErrorCode::ValidationFailed, "Field 'payload' must be an object or null");
    }

    return core::Result::Success();
}

void JsonProtocol::build_response_envelope(
    const std::string& message_id,
    const core::Result& result,
    const JsonDocument& payload,
    JsonDocument& out_envelope) {
    out_envelope.clear();

    out_envelope["protocol_version"] = "1.0";
    out_envelope["kind"] = "response";
    out_envelope["message_id"] = message_id;
    out_envelope["timestamp_ms"] = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

    if (result.code == core::ErrorCode::Ok) {
        out_envelope["status"] = "ok";
    } else if (result.code == core::ErrorCode::RestartRequired) {
        out_envelope["status"] = "restart_required";
    } else {
        out_envelope["status"] = "error";
    }

    out_envelope["error_code"] = core::ToString(result.code);
    if (!result.message.empty()) {
        out_envelope["message"] = result.message;
    }

    out_envelope["payload"] = payload.as<JsonVariantConst>();
}

std::string JsonProtocol::serialize_line(const JsonDocument& document) {
    std::string line;
    serializeJson(document, line);
    line.push_back('\n');
    return line;
}

}  // namespace slidr::communication
