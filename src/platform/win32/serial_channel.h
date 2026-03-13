#pragma once

// Simple serial-based IChannel implementation.

#include <windows.h>
#include <string>
#include <vector>

#include "transport/channel.h"

namespace canmatik {

class SerialChannel : public IChannel {
public:
    explicit SerialChannel(std::string port_name);
    ~SerialChannel() override;

    void open(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN) override;
    void close() override;
    std::vector<CanFrame> read(uint32_t timeout_ms) override;
    void write(const CanFrame& frame) override;
    void setFilter(uint32_t mask, uint32_t pattern) override;
    void clearFilters() override;
    bool isOpen() const override;

private:
    std::string port_name_;
    // Expose handle_ for bridge use (internal visibility)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    uint32_t baud_ = 115200;
};

} // namespace canmatik
