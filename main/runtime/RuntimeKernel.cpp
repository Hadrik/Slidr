#include "runtime/RuntimeKernel.h"

#include <vector>

#include "config/ConfigSchema.h"
#include "esp_log.h"
#include "RuntimeKernel.h"

namespace {
constexpr const char* kTag = "RuntimeKernel";
}

namespace slidr::runtime {

void RuntimeKernel::initialize() {
    if (_initialized) {
        return;
    }

    register_default_schemas();
    register_default_handlers();

    const core::Result fs_result = _filesystem.initialize();
    if (!fs_result.ok()) {
        ESP_LOGW(kTag, "Filesystem init failed: %s", fs_result.message.c_str());
    }

    const core::Result transfer_result = _transfer_manager.initialize(_filesystem);
    if (!transfer_result.ok()) {
        ESP_LOGW(kTag, "Transfer manager init failed: %s", transfer_result.message.c_str());
    }

    if (_event_sink) {
        _transfer_manager.set_event_sink(_event_sink);
    }

    static constexpr const char* kBootstrapConfig = R"json(
{
  "components": [
    {
      "id": "slider_0",
      "type": "slider",
      "options": {
        "pin": 1,
        "min_value": 0,
        "max_value": 100,
        "deadzone": 2,
        "adc_bits": 12
      }
    },
    {
      "id": "led_0",
      "type": "led",
      "options": {
        "pin": 15
      }
    }
  ]
}
)json";

    std::string config_json;
    const core::Result config_result = get_config_from_filesystem(config_json);
    const auto load_default = [&]() {
        const core::Result load_result = _config_engine.load_desired_config_from_json(kBootstrapConfig);
        if (!load_result.ok()) {
            ESP_LOGE(kTag, "Failed to load bootstrap config: %s", load_result.message.c_str());
        }
    };
    if (config_result.ok()) {
        const core::Result load_result = _config_engine.load_desired_config_from_json(config_json);
        if (!load_result.ok()) {
            ESP_LOGW(kTag, "Failed to load config from filesystem, loading bootstrap config: %s", load_result.message.c_str());
            load_default();
        }
    } else {
        ESP_LOGW(kTag, "Loading bootstrap config: %s", config_result.message.c_str());
        load_default();
    }

    _initialized = true;
}

core::Result RuntimeKernel::handle_line(const std::string& line, std::string& out_line) {
    return _gateway.handle_incoming_line(line, out_line);
}

void RuntimeKernel::set_event_sink(std::function<void(std::string)> sink) {
    _event_sink = std::move(sink);
    _transfer_manager.set_event_sink(_event_sink);
}

void RuntimeKernel::register_default_schemas() {
    config::ComponentSchema slider_schema {
        .type = "slider",
        .fields = {
            {"pin", config::FieldSchema {
                .name = "pin",
                .mutability = config::Mutability::RestartRequired,
                .validator = config::validators::in_range_int(0, 48),
            }},
            {"min_value", config::FieldSchema {
                .name = "min_value",
                .mutability = config::Mutability::RuntimeMutable,
                .validator = config::validators::in_range_int(0, 100),
            }},
            {"max_value", config::FieldSchema {
                .name = "max_value",
                .mutability = config::Mutability::RuntimeMutable,
                .validator = config::validators::in_range_int(0, 100),
            }},
            {"deadzone", config::FieldSchema {
                .name = "deadzone",
                .mutability = config::Mutability::RuntimeMutable,
                .validator = config::validators::in_range_int(0, 50),
            }},
            {"adc_bits", config::FieldSchema {
                .name = "adc_bits",
                .mutability = config::Mutability::RestartRequired,
                .validator = config::validators::in_range_int(9, 13),
            }},
        }
    };
    (void) _config_engine.register_schema(std::move(slider_schema));

    config::ComponentSchema led_schema {
        .type = "led",
        .fields = {
            {"pin", config::FieldSchema {
                .name = "pin",
                .mutability = config::Mutability::RestartRequired,
                .validator = config::validators::in_range_int(0, 48),
            }},
        }
    };
    (void) _config_engine.register_schema(std::move(led_schema));
}

void RuntimeKernel::register_default_handlers() {
    _gateway.register_handler("ping", [](const JsonObjectConst&, JsonDocument& response_payload) {
        response_payload["pong"] = true;
        return core::Result::Success();
    });

    _gateway.register_handler("heartbeat", [this](const JsonObjectConst&, JsonDocument& response_payload) {
        response_payload["alive"] = true;
        response_payload["filesystem_ready"] = _filesystem.ready();

        JsonArray pending = response_payload["pending_restart_components"].to<JsonArray>();
        for (const auto& id : _config_engine.pending_restart_components()) {
            pending.add(id);
        }

        return core::Result::Success();
    });

    _gateway.register_handler("config.get", [this](const JsonObjectConst&, JsonDocument& response_payload) {
        JsonDocument desired;
        core::Result result = _config_engine.get_desired_config(desired);
        if (!result.ok()) {
            return result;
        }

        response_payload["document"] = desired.as<JsonVariantConst>();
        return core::Result::Success();
    });

    _gateway.register_handler("config.save", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!_filesystem.ready()) {
            return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not ready");
        }
        
        JsonDocument config;
        const core::Result result = _config_engine.get_desired_config(config);
        if (!result.ok()) {
            return result;
        }

        std::string config_json;
        serializeJson(config, config_json);

        std::string path = kConfigPath;
        if (payload["path"].is<const char*>()) {
            path = payload["path"].as<std::string>();
        }

        const core::Result write_result = _filesystem.write_text_file_atomic(path, config_json);
        if (!write_result.ok()) {
            return core::Result::Failure(core::ErrorCode::FileSystemError, "Failed to write config to filesystem: " + write_result.message);
        }

        response_payload["saved"] = true;
        response_payload["path"] = path;
        return core::Result::Success();
    });

    _gateway.register_handler("config.apply_full", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["document"].is<JsonObject>()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.apply_full requires object field 'document'");
        }

        JsonDocument document;
        document.set(payload["document"].as<JsonObjectConst>());

        core::Result result = _config_engine.load_desired_config(document);
        if (result.ok()) {
            response_payload["applied"] = true;
        }
        return result;
    });

    _gateway.register_handler("config.update_field", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["component_id"].is<const char*>()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.update_field requires string field 'component_id'");
        }
        if (!payload["field"].is<const char*>()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.update_field requires string field 'field'");
        }
        if (payload["value"].isNull()) {
            return core::Result::Failure(
                core::ErrorCode::MissingField,
                "config.update_field requires field 'value'");
        }

        const std::string component_id = payload["component_id"].as<std::string>();
        const std::string field_name = payload["field"].as<std::string>();

        bool restart_required = false;
        core::Result result = _config_engine.apply_targeted_update(
            component_id,
            field_name,
            payload["value"],
            restart_required);

        response_payload["component_id"] = component_id;
        response_payload["field"] = field_name;
        response_payload["restart_required"] = restart_required;

        if (restart_required) {
            JsonArray pending = response_payload["pending_restart_components"].to<JsonArray>();
            for (const auto& id : _config_engine.pending_restart_components()) {
                pending.add(id);
            }
        }

        return result;
    });

    _gateway.register_handler("file.list", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        const std::string path = payload["path"] | "/";

        std::vector<services::FileMetadata> entries;
        const core::Result result = _filesystem.list(path, entries);
        if (!result.ok()) {
            return result;
        }

        response_payload["path"] = path;
        response_payload["count"] = static_cast<uint32_t>(entries.size());

        JsonArray json_entries = response_payload["entries"].to<JsonArray>();
        for (const auto& entry : entries) {
            JsonObject item = json_entries.add<JsonObject>();
            item["path"] = entry.path;
            item["size"] = static_cast<uint32_t>(entry.size);
            item["modified_ms"] = entry.modified_ms;
            item["is_directory"] = entry.is_directory;
        }

        return core::Result::Success();
    });

    _gateway.register_handler("file.stat", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["path"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.stat requires string field 'path'");
        }

        const std::string path = payload["path"].as<std::string>();

        services::FileMetadata metadata;
        const core::Result result = _filesystem.stat(path, metadata);
        if (!result.ok()) {
            return result;
        }

        response_payload["path"] = metadata.path;
        response_payload["size"] = static_cast<uint32_t>(metadata.size);
        response_payload["modified_ms"] = metadata.modified_ms;
        response_payload["is_directory"] = metadata.is_directory;
        return core::Result::Success();
    });

    _gateway.register_handler("file.delete", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["path"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.delete requires string field 'path'");
        }

        const std::string path = payload["path"].as<std::string>();
        const core::Result result = _filesystem.delete_file(path);
        if (!result.ok()) {
            return result;
        }

        response_payload["path"] = path;
        response_payload["deleted"] = true;
        return core::Result::Success();
    });

    _gateway.register_handler("file.upload.start", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["path"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.start requires string field 'path'");
        }
        if (!payload["total_size"].is<uint32_t>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.start requires numeric field 'total_size'");
        }

        const std::string path = payload["path"].as<std::string>();
        const std::size_t total_size = payload["total_size"].as<uint32_t>();
        const std::size_t chunk_size = payload["chunk_size"] | 0;

        communication::FileTransferManager::UploadStartInfo info;
        const core::Result result = _transfer_manager.begin_upload(path, total_size, chunk_size, info);
        if (!result.ok()) {
            return result;
        }

        response_payload["session_id"] = info.session_id;
        response_payload["chunk_size"] = static_cast<uint32_t>(info.chunk_size);
        response_payload["total_size"] = static_cast<uint32_t>(info.total_size);
        response_payload["expected_chunks"] = static_cast<uint32_t>(info.expected_chunks);
        return core::Result::Success();
    });

    _gateway.register_handler("file.upload.chunk", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["session_id"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.chunk requires string field 'session_id'");
        }
        if (!payload["index"].is<uint32_t>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.chunk requires numeric field 'index'");
        }
        if (!payload["data_base64"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.chunk requires string field 'data_base64'");
        }
        if (!payload["chunk_crc32"].is<uint32_t>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.chunk requires numeric field 'chunk_crc32'");
        }

        const std::string session_id = payload["session_id"].as<std::string>();
        const std::size_t index = payload["index"].as<uint32_t>();
        const std::string data_base64 = payload["data_base64"].as<std::string>();
        const uint32_t chunk_crc32 = payload["chunk_crc32"].as<uint32_t>();

        communication::FileTransferManager::UploadChunkInfo info;
        const core::Result result = _transfer_manager.accept_upload_chunk(session_id, index, data_base64, chunk_crc32, info);
        if (!result.ok()) {
            return result;
        }

        response_payload["bytes_received"] = static_cast<uint32_t>(info.bytes_received);
        response_payload["total_size"] = static_cast<uint32_t>(info.total_size);
        response_payload["next_index"] = static_cast<uint32_t>(info.next_index);
        return core::Result::Success();
    });

    _gateway.register_handler("file.upload.finish", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["session_id"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.finish requires string field 'session_id'");
        }

        const std::string session_id = payload["session_id"].as<std::string>();
        const uint32_t total_crc32 = payload["total_crc32"] | 0;

        const core::Result result = _transfer_manager.finish_upload(session_id, total_crc32);
        if (!result.ok()) {
            return result;
        }

        response_payload["session_id"] = session_id;
        response_payload["completed"] = true;
        return core::Result::Success();
    });

    _gateway.register_handler("file.upload.cancel", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["session_id"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.upload.cancel requires string field 'session_id'");
        }

        const std::string session_id = payload["session_id"].as<std::string>();
        const core::Result result = _transfer_manager.cancel_upload(session_id);
        if (!result.ok()) {
            return result;
        }

        response_payload["session_id"] = session_id;
        response_payload["cancelled"] = true;
        return core::Result::Success();
    });

    _gateway.register_handler("file.download.start", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["path"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.download.start requires string field 'path'");
        }

        const std::string path = payload["path"].as<std::string>();
        const std::size_t chunk_size = payload["chunk_size"] | 0;

        communication::FileTransferManager::DownloadStartInfo info;
        const core::Result result = _transfer_manager.begin_download(path, chunk_size, info);
        if (!result.ok()) {
            return result;
        }

        response_payload["session_id"] = info.session_id;
        response_payload["chunk_size"] = static_cast<uint32_t>(info.chunk_size);
        response_payload["total_size"] = static_cast<uint32_t>(info.total_size);
        response_payload["total_chunks"] = static_cast<uint32_t>(info.total_chunks);
        response_payload["total_crc32"] = info.total_crc32;
        return core::Result::Success();
    });

    _gateway.register_handler("file.download.chunk", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["session_id"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.download.chunk requires string field 'session_id'");
        }
        if (!payload["index"].is<uint32_t>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.download.chunk requires numeric field 'index'");
        }

        const std::string session_id = payload["session_id"].as<std::string>();
        const std::size_t index = payload["index"].as<uint32_t>();

        communication::FileTransferManager::DownloadChunkInfo info;
        const core::Result result = _transfer_manager.get_download_chunk(session_id, index, info);
        if (!result.ok()) {
            return result;
        }

        response_payload["session_id"] = session_id;
        response_payload["index"] = static_cast<uint32_t>(info.chunk_index);
        response_payload["chunk_size"] = static_cast<uint32_t>(info.chunk_size);
        response_payload["chunk_crc32"] = info.chunk_crc32;
        response_payload["is_last"] = info.is_last;
        response_payload["data_base64"] = info.chunk_base64;
        return core::Result::Success();
    });

    _gateway.register_handler("file.download.finish", [this](const JsonObjectConst& payload, JsonDocument& response_payload) {
        if (!payload["session_id"].is<const char*>()) {
            return core::Result::Failure(core::ErrorCode::MissingField, "file.download.finish requires string field 'session_id'");
        }

        const std::string session_id = payload["session_id"].as<std::string>();
        const core::Result result = _transfer_manager.finish_download(session_id);
        if (!result.ok()) {
            return result;
        }

        response_payload["session_id"] = session_id;
        response_payload["completed"] = true;
        return core::Result::Success();
    });
}

core::Result RuntimeKernel::get_config_from_filesystem(std::string& out_config_json) {
    if (!_filesystem.ready()) {
        return core::Result::Failure(core::ErrorCode::FileSystemUnavailable, "Filesystem not ready");
    }

    const core::Result result = _filesystem.read_text_file(kConfigPath, out_config_json);
    if (!result.ok()) {
        ESP_LOGW(kTag, "Failed to read config from filesystem: %s", result.message.c_str());
        return core::Result::Failure(core::ErrorCode::FileNotFound, "Failed to read config from filesystem");
    }

    return core::Result::Success();
}

}  // namespace slidr::runtime
