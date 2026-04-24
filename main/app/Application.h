#ifndef SLIDR_APP_APPLICATION_H
#define SLIDR_APP_APPLICATION_H

#pragma once

#include <string>

#include "core/Result.h"
#include "runtime/RuntimeKernel.h"

namespace slidr::app {

class Application {
public:
    void begin();
    void tick();

    core::Result process_line(const std::string& line, std::string& out_line);

private:
    runtime::RuntimeKernel _kernel;
};

}  // namespace slidr::app

#endif
