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
    InternalError,
    QueueFull,
    Timeout,
    Busy,
    FileSystemUnavailable,
    FileSystemError,
    FileNotFound,
    PathNotAllowed,
    TransferSessionNotFound,
    TransferCrcMismatch,
    TransferOutOfOrder,
    TransferSizeMismatch,
    TransferDecodeFailed
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
        case ErrorCode::QueueFull:
            return "queue_full";
        case ErrorCode::Timeout:
            return "timeout";
        case ErrorCode::Busy:
            return "busy";
        case ErrorCode::FileSystemUnavailable:
            return "filesystem_unavailable";
        case ErrorCode::FileSystemError:
            return "filesystem_error";
        case ErrorCode::FileNotFound:
            return "file_not_found";
        case ErrorCode::PathNotAllowed:
            return "path_not_allowed";
        case ErrorCode::TransferSessionNotFound:
            return "transfer_session_not_found";
        case ErrorCode::TransferCrcMismatch:
            return "transfer_crc_mismatch";
        case ErrorCode::TransferOutOfOrder:
            return "transfer_out_of_order";
        case ErrorCode::TransferSizeMismatch:
            return "transfer_size_mismatch";
        case ErrorCode::TransferDecodeFailed:
            return "transfer_decode_failed";
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
