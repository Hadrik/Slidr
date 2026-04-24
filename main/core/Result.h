#ifndef SLIDR_CORE_RESULT_H
#define SLIDR_CORE_RESULT_H

#pragma once

#include <string>

namespace slidr::core {

enum class ErrorCode {
    Ok = 0,
    InvalidJson,
    MissingField,
    UnknownCommand,
    ValidationFailed,
    NotFound,
    RestartRequired,
    InternalError
};

inline const char* ToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return "ok";
        case ErrorCode::InvalidJson:
            return "invalid_json";
        case ErrorCode::MissingField:
            return "missing_field";
        case ErrorCode::UnknownCommand:
            return "unknown_command";
        case ErrorCode::ValidationFailed:
            return "validation_failed";
        case ErrorCode::NotFound:
            return "not_found";
        case ErrorCode::RestartRequired:
            return "restart_required";
        case ErrorCode::InternalError:
            return "internal_error";
    }
    return "unknown";
}

struct Result {
    ErrorCode code = ErrorCode::Ok;
    std::string message {};

    [[nodiscard]] bool ok() const {
        return code == ErrorCode::Ok;
    }

    static Result Success() {
        return {};
    }

    static Result Failure(ErrorCode error, std::string details) {
        return Result {
            .code = error,
            .message = std::move(details),
        };
    }
};

}  // namespace slidr::core

#endif
