#ifndef SLIDR_RUNTIME_RUNTIME_KERNEL_H
#define SLIDR_RUNTIME_RUNTIME_KERNEL_H

#pragma once

#include <string>

#include "communication/MessageGateway.h"
#include "config/ConfigEngine.h"
#include "core/Result.h"
#include "services/FilesystemService.h"

namespace slidr::runtime {

class RuntimeKernel {
public:
    void initialize();
    core::Result handle_line(const std::string& line, std::string& out_line);

private:
    void register_default_schemas();
    void register_default_handlers();

    communication::MessageGateway _gateway;
    config::ConfigEngine _config_engine;
    services::FilesystemService _filesystem;
    bool _initialized = false;
};

}  // namespace slidr::runtime

#endif
