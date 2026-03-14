#include "platform/win32/serial_channel.h"

#include <algorithm>
#include <stdexcept>

#include "core/log_macros.h"

namespace canmatik {

SerialChannel::SerialChannel(std::string port_name)
    : port_name_(std::move(port_name)) {}

SerialChannel::~SerialChannel() {
    close();
}

void SerialChannel::open(uint32_t bitrate, BusProtocol /*protocol*/) {
    if (isOpen()) return;
    baud_ = bitrate ? bitrate : 115200;

    std::string path = "\\\\.\\" + port_name_;
    handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                          0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to open serial port {} (err={})", port_name_, GetLastError());
        throw std::runtime_error("Failed to open serial port");
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        throw std::runtime_error("GetCommState failed");
    }
    dcb.BaudRate = baud_;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(handle_, &dcb);

    COMMTIMEOUTS to = {};
    to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 50;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(handle_, &to);
}

void SerialChannel::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

std::vector<CanFrame> SerialChannel::read(uint32_t timeout_ms) {
    std::vector<CanFrame> out;
    if (handle_ == INVALID_HANDLE_VALUE) return out;

    // Adjust timeouts for this call
    COMMTIMEOUTS to = {};
    to.ReadIntervalTimeout = timeout_ms;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = timeout_ms;
    SetCommTimeouts(handle_, &to);

    uint8_t buf[64];
    DWORD read = 0;
    if (!ReadFile(handle_, buf, static_cast<DWORD>(sizeof(buf)), &read, nullptr)) {
        // ReadFile failed — return empty
        return out;
    }

    if (read == 0) return out;

    // Package all read bytes into CanFrames (up to 8 bytes each)
    DWORD off = 0;
    while (off < read) {
        CanFrame f{};
        f.arbitration_id = 0x7FF; // sentinel: serial
        f.type = FrameType::Standard;
        DWORD chunk = std::min<DWORD>(read - off, 8);
        f.dlc = static_cast<uint8_t>(chunk);
        for (DWORD i = 0; i < chunk; ++i) f.data[i] = buf[off + i];
        f.host_timestamp_us = 0;
        out.push_back(f);
        off += chunk;
    }
    return out;
}

void SerialChannel::write(const CanFrame& frame) {
    if (handle_ == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    DWORD to_write = frame.dlc;
    // Write raw payload bytes
    WriteFile(handle_, frame.data.data(), to_write, &written, nullptr);
}

void SerialChannel::setFilter(uint32_t /*mask*/, uint32_t /*pattern*/) {}
void SerialChannel::clearFilters() {}
bool SerialChannel::isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

} // namespace canmatik
