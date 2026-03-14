#pragma once

#include <memory>
#include <string>
#include <atomic>

namespace canmatik { class ICaptureSync; }

namespace canmatik {

class SessionService;

class Elm327Bridge {
public:
    Elm327Bridge(std::string serial_port, std::string provider_name, ICaptureSync* frame_sink = nullptr, bool terminated = false, uint32_t baud = 38400, std::string mock_script = "");
    ~Elm327Bridge();

    // Run the bridge (blocking). Returns when user interrupts or on error.
    int run();

    // Request the bridge to stop; thread-safe.
    void stop();
    // If run() failed or stopped, this returns a human-readable message.
    std::string last_error() const;

private:
    std::string serial_port_;
    std::string provider_name_;
    std::unique_ptr<SessionService> session_;
    ICaptureSync* frame_sink_ = nullptr;
    std::atomic<bool> stop_flag_{false};
    std::string last_error_; // protected by internal usage from single thread
    bool terminated_mode_ = false;
    uint32_t serial_baud_ = 38400;
    std::string mock_script_path_;
    void handle_line(const std::string& line);
};

} // namespace canmatik
