#pragma once

/// @file proxy_server.h
/// Named-pipe proxy server.  Listens for a fake J2534 DLL client, forwards
/// every J2534 API call to the real adapter that CANmatik already has open,
/// and emits CAN frames for the GUI capture pipeline.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "proxy/proxy_protocol.h"
#include "core/capture_sink.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace canmatik {

// Forward declarations — the server only needs the DLL-level J2534 API.
namespace j2534 {
    struct PASSTHRU_MSG;
}

class J2534DllLoader;

/// Callback fired for every CAN frame that the proxy observes (TX or RX).
/// The GUI wires this to FrameCollector::push().
using ProxyFrameCallback = std::function<void(const CanFrame&)>;

class ProxyServer {
public:
    ProxyServer();
    ~ProxyServer();

    /// Start the pipe server thread (proxy mode — forwards to real adapter).
    /// @param loader  A connected J2534DllLoader for the real adapter.
    /// @param on_frame  Called for every CAN frame flowing through the proxy.
    /// @return Empty on success, error message on failure.
    std::string start(J2534DllLoader& loader, ProxyFrameCallback on_frame);

    /// Start the pipe server in terminated mode (no real adapter).
    /// Accepts all J2534 calls locally and emits TX frames for GUI display.
    /// @param on_frame  Called for every CAN frame the external tool writes.
    /// @return Empty on success, error message on failure.
    std::string start_terminated(ProxyFrameCallback on_frame);

    /// Signal the server to stop and wait for the thread to exit.
    void stop();

    bool is_running() const { return running_.load(); }

    /// True if an external tool is currently connected to the pipe.
    bool has_client() const { return client_connected_.load(); }

private:
    void server_thread(J2534DllLoader& loader);
    void server_thread_terminated();
    void handle_client(HANDLE pipe, J2534DllLoader& loader);
    void handle_client_terminated(HANDLE pipe);

    // Dispatch individual J2534 calls
    void dispatch(HANDLE pipe, const proxy::ProxyRequest& hdr,
                  const std::vector<uint8_t>& payload,
                  J2534DllLoader& loader);
    void dispatch_terminated(HANDLE pipe, const proxy::ProxyRequest& hdr,
                             const std::vector<uint8_t>& payload);

    void send_response(HANDLE pipe, int32_t status,
                       const void* data, uint32_t size);

    // Convert between WireMsg and PASSTHRU_MSG
    static void wire_to_passthru(const proxy::WireMsg& w, j2534::PASSTHRU_MSG& m);
    static void passthru_to_wire(const j2534::PASSTHRU_MSG& m, proxy::WireMsg& w);

    // Emit a CanFrame from a PASSTHRU_MSG (for GUI display)
    void emit_frame(const j2534::PASSTHRU_MSG& msg, bool is_tx);

    std::thread         thread_;
    std::atomic<bool>   running_{false};
    std::atomic<bool>   stop_requested_{false};
    std::atomic<bool>   client_connected_{false};
    bool                terminated_mode_{false};
    ProxyFrameCallback  on_frame_;
    std::mutex          callback_mutex_;

    // Terminated-mode virtual device state
    struct TermState {
        bool device_open  = false;
        bool channel_open = false;
        unsigned long protocol   = 0;
        unsigned long baudrate   = 0;
        unsigned long next_filter = 1;
        unsigned long next_periodic = 1;
        char last_error[80] = {};
    };
    TermState term_state_;
};

} // namespace canmatik
