#ifndef SLIDR_COMMUNICATION_JSON_LINE_TRANSPORT_H
#define SLIDR_COMMUNICATION_JSON_LINE_TRANSPORT_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "core/Result.h"

namespace slidr::communication {

class JsonLineTransport {
public:
    struct Config {
        std::size_t max_line_length = 4096;
        std::size_t max_incoming_queue = 16;
        std::size_t max_outgoing_queue = 16;
        std::size_t max_rx_bytes_per_poll = 512;
        uint32_t partial_line_timeout_ms = 2000;
    };

    explicit JsonLineTransport(Config config = _default_config());

    core::Result begin();
    void poll();

    bool try_pop_incoming_line(std::string& out_line);
    core::Result queue_outgoing_line(std::string line);

private:
    void drain_rx();
    void drain_tx();

    Config _config;
    bool _initialized = false;

    std::deque<std::string> _incoming_lines {};
    std::deque<std::string> _outgoing_lines {};

    std::string _rx_buffer {};
    uint64_t _rx_started_ms = 0;

    std::string _active_tx_line {};
    std::size_t _active_tx_offset = 0;

    static Config _default_config() { return {}; }; // GCC bug 36684
};

}  // namespace slidr::communication

#endif
