/// @file proxy_server.cpp
/// Named-pipe proxy server implementation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "proxy/proxy_server.h"
#include "platform/win32/j2534_dll_loader.h"
#include "platform/win32/j2534_defs.h"
#include "core/can_frame.h"
#include "core/timestamp.h"
#include "core/log_macros.h"

#include <cstring>
#include <vector>

namespace canmatik {

using namespace proxy;

// ---------------------------------------------------------------------------
// Helpers: read/write exact byte counts from a pipe
// ---------------------------------------------------------------------------
static bool pipe_read(HANDLE h, void* buf, uint32_t len) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    uint32_t remaining = len;
    while (remaining > 0) {
        DWORD got = 0;
        if (!ReadFile(h, p, remaining, &got, nullptr) || got == 0)
            return false;
        p += got;
        remaining -= got;
    }
    return true;
}

static bool pipe_write(HANDLE h, const void* buf, uint32_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    uint32_t remaining = len;
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(h, p, remaining, &written, nullptr) || written == 0)
            return false;
        p += written;
        remaining -= written;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
ProxyServer::ProxyServer() = default;

ProxyServer::~ProxyServer() {
    stop();
}

// ---------------------------------------------------------------------------
// Start / stop
// ---------------------------------------------------------------------------
std::string ProxyServer::start(J2534DllLoader& loader, ProxyFrameCallback on_frame) {
    if (running_.load())
        return "Proxy server already running";

    on_frame_ = std::move(on_frame);
    terminated_mode_ = false;
    stop_requested_.store(false);
    running_.store(true);

    thread_ = std::thread([this, &loader]{ server_thread(loader); });

    return {};
}

std::string ProxyServer::start_terminated(ProxyFrameCallback on_frame) {
    if (running_.load())
        return "Proxy server already running";

    on_frame_ = std::move(on_frame);
    terminated_mode_ = true;
    term_state_ = TermState{};
    stop_requested_.store(false);
    running_.store(true);

    thread_ = std::thread([this]{ server_thread_terminated(); });

    return {};
}

void ProxyServer::stop() {
    stop_requested_.store(true);

    // Wake the pipe server out of ConnectNamedPipe by briefly connecting
    if (running_.load()) {
        HANDLE h = CreateFileA(kPipeName, GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }

    if (thread_.joinable())
        thread_.join();
    running_.store(false);
}

// ---------------------------------------------------------------------------
// Server thread: create pipe, accept connections in a loop
// ---------------------------------------------------------------------------
void ProxyServer::server_thread(J2534DllLoader& loader) {
    LOG_INFO("ProxyServer: listening on {}", kPipeName);

    while (!stop_requested_.load()) {
        HANDLE pipe = CreateNamedPipeA(
            kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,                  // single instance
            kPipeBufferSize,
            kPipeBufferSize,
            0,                  // default timeout
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            LOG_ERROR("ProxyServer: CreateNamedPipe failed (err={})", GetLastError());
            break;
        }

        // Wait for a client (blocks until connection or error)
        BOOL connected = ConnectNamedPipe(pipe, nullptr)
                       ? TRUE
                       : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);

        if (!connected || stop_requested_.load()) {
            CloseHandle(pipe);
            continue;
        }

        client_connected_.store(true);
        LOG_INFO("ProxyServer: client connected");

        handle_client(pipe, loader);

        client_connected_.store(false);
        LOG_INFO("ProxyServer: client disconnected");

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    running_.store(false);
    LOG_INFO("ProxyServer: stopped");
}

// ---------------------------------------------------------------------------
// Server thread (terminated mode): accept connections, handle locally
// ---------------------------------------------------------------------------
void ProxyServer::server_thread_terminated() {
    LOG_INFO("ProxyServer [terminated]: listening on {}", kPipeName);

    while (!stop_requested_.load()) {
        HANDLE pipe = CreateNamedPipeA(
            kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            kPipeBufferSize,
            kPipeBufferSize,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            LOG_ERROR("ProxyServer [terminated]: CreateNamedPipe failed (err={})", GetLastError());
            break;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr)
                       ? TRUE
                       : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);

        if (!connected || stop_requested_.load()) {
            CloseHandle(pipe);
            continue;
        }

        client_connected_.store(true);
        term_state_ = TermState{};
        LOG_INFO("ProxyServer [terminated]: client connected");

        handle_client_terminated(pipe);

        client_connected_.store(false);
        LOG_INFO("ProxyServer [terminated]: client disconnected");

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    running_.store(false);
    LOG_INFO("ProxyServer [terminated]: stopped");
}

// ---------------------------------------------------------------------------
// Handle one client session (terminated mode)
// ---------------------------------------------------------------------------
void ProxyServer::handle_client_terminated(HANDLE pipe) {
    while (!stop_requested_.load()) {
        ProxyRequest hdr{};
        if (!pipe_read(pipe, &hdr, sizeof(hdr)))
            break;

        std::vector<uint8_t> payload;
        if (hdr.payload_size > 0) {
            if (hdr.payload_size > 1024 * 1024) break;
            payload.resize(hdr.payload_size);
            if (!pipe_read(pipe, payload.data(), hdr.payload_size))
                break;
        }

        dispatch_terminated(pipe, hdr, payload);
    }
}

// ---------------------------------------------------------------------------
// Handle one client session — read requests, dispatch, repeat
// ---------------------------------------------------------------------------
void ProxyServer::handle_client(HANDLE pipe, J2534DllLoader& loader) {
    while (!stop_requested_.load()) {
        ProxyRequest hdr{};
        if (!pipe_read(pipe, &hdr, sizeof(hdr)))
            break;  // client disconnected

        std::vector<uint8_t> payload;
        if (hdr.payload_size > 0) {
            if (hdr.payload_size > 1024 * 1024) break; // sanity limit
            payload.resize(hdr.payload_size);
            if (!pipe_read(pipe, payload.data(), hdr.payload_size))
                break;
        }

        dispatch(pipe, hdr, payload, loader);
    }
}

// ---------------------------------------------------------------------------
// Response helper
// ---------------------------------------------------------------------------
void ProxyServer::send_response(HANDLE pipe, int32_t status,
                                const void* data, uint32_t size) {
    ProxyResponse resp{};
    resp.status = status;
    resp.payload_size = size;
    pipe_write(pipe, &resp, sizeof(resp));
    if (size > 0 && data)
        pipe_write(pipe, data, size);
}

// ---------------------------------------------------------------------------
// WireMsg <-> PASSTHRU_MSG conversion
// ---------------------------------------------------------------------------
void ProxyServer::wire_to_passthru(const WireMsg& w, j2534::PASSTHRU_MSG& m) {
    std::memset(&m, 0, sizeof(m));
    m.ProtocolID     = w.ProtocolID;
    m.RxStatus       = w.RxStatus;
    m.TxFlags        = w.TxFlags;
    m.Timestamp      = w.Timestamp;
    m.DataSize       = w.DataSize;
    m.ExtraDataIndex = w.ExtraDataIndex;
    uint32_t copy_len = (w.DataSize <= j2534::PASSTHRU_MSG_DATA_SIZE)
                      ? w.DataSize : j2534::PASSTHRU_MSG_DATA_SIZE;
    std::memcpy(m.Data, w.Data, copy_len);
}

void ProxyServer::passthru_to_wire(const j2534::PASSTHRU_MSG& m, WireMsg& w) {
    w.ProtocolID     = m.ProtocolID;
    w.RxStatus       = m.RxStatus;
    w.TxFlags        = m.TxFlags;
    w.Timestamp      = m.Timestamp;
    w.DataSize       = m.DataSize;
    w.ExtraDataIndex = m.ExtraDataIndex;
    uint32_t copy_len = (m.DataSize <= sizeof(w.Data)) ? m.DataSize : sizeof(w.Data);
    std::memcpy(w.Data, m.Data, copy_len);
}

// ---------------------------------------------------------------------------
// Emit CanFrame from a PASSTHRU_MSG (for GUI display)
// ---------------------------------------------------------------------------
void ProxyServer::emit_frame(const j2534::PASSTHRU_MSG& msg, bool is_tx) {
    // Only CAN / ISO15765 frames have meaningful content for display
    if (msg.DataSize < 4) return;

    CanFrame f{};
    // First 4 bytes of Data = CAN ID (big-endian in J2534)
    f.arbitration_id = (static_cast<uint32_t>(msg.Data[0]) << 24)
                     | (static_cast<uint32_t>(msg.Data[1]) << 16)
                     | (static_cast<uint32_t>(msg.Data[2]) << 8)
                     | (static_cast<uint32_t>(msg.Data[3]));

    uint32_t payload_len = msg.DataSize - 4;
    f.dlc = static_cast<uint8_t>(payload_len > 8 ? 8 : payload_len);
    for (uint8_t i = 0; i < f.dlc; ++i)
        f.data[i] = msg.Data[4 + i];

    f.host_timestamp_us = host_timestamp_us();
    bool ext = (msg.TxFlags & j2534::CAN_29BIT_ID) != 0
            || (msg.RxStatus & j2534::RX_MSG_CAN_29BIT_ID) != 0;
    f.type = ext ? FrameType::Extended : FrameType::Standard;

    std::lock_guard<std::mutex> lk(callback_mutex_);
    if (on_frame_) on_frame_(f);
}

// ---------------------------------------------------------------------------
// Dispatch: decode request, call real J2534 adapter, send response
// ---------------------------------------------------------------------------
void ProxyServer::dispatch(HANDLE pipe, const ProxyRequest& hdr,
                           const std::vector<uint8_t>& payload,
                           J2534DllLoader& loader) {
    switch (static_cast<FuncId>(hdr.func_id)) {

    // --- PassThruOpen ---
    case FUNC_OPEN: {
        OpenRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        unsigned long device_id = 0;
        void* name_ptr = (req.name[0] != '\0')
                       ? static_cast<void*>(req.name)
                       : nullptr;
        long status = loader.PassThruOpen(name_ptr, &device_id);

        OpenResponse resp{};
        resp.device_id = device_id;
        send_response(pipe, status, &resp, sizeof(resp));
        break;
    }

    // --- PassThruClose ---
    case FUNC_CLOSE: {
        CloseRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        long status = loader.PassThruClose(req.device_id);
        send_response(pipe, status, nullptr, 0);
        break;
    }

    // --- PassThruConnect ---
    case FUNC_CONNECT: {
        ConnectRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        unsigned long channel_id = 0;
        long status = loader.PassThruConnect(
            req.device_id, req.protocol_id, req.flags, req.baudrate, &channel_id);

        ConnectResponse resp{};
        resp.channel_id = channel_id;
        send_response(pipe, status, &resp, sizeof(resp));
        break;
    }

    // --- PassThruDisconnect ---
    case FUNC_DISCONNECT: {
        DisconnectRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        long status = loader.PassThruDisconnect(req.channel_id);
        send_response(pipe, status, nullptr, 0);
        break;
    }

    // --- PassThruReadMsgs ---
    case FUNC_READ_MSGS: {
        ReadMsgsRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        // Allocate message buffer
        uint32_t count = req.max_msgs;
        if (count > 64) count = 64;
        std::vector<j2534::PASSTHRU_MSG> msgs(count);
        unsigned long num_msgs = count;

        long status = loader.PassThruReadMsgs(
            req.channel_id, msgs.data(), &num_msgs, req.timeout);

        // Build response: [num_msgs:u32] [WireMsg × num_msgs]
        std::vector<uint8_t> resp_buf;
        resp_buf.resize(sizeof(uint32_t));
        uint32_t n = num_msgs;
        std::memcpy(resp_buf.data(), &n, sizeof(n));

        for (uint32_t i = 0; i < n; ++i) {
            // Skip TX echo frames
            if (msgs[i].RxStatus & j2534::RX_MSG_TX_DONE) continue;

            emit_frame(msgs[i], false);

            WireMsg w{};
            passthru_to_wire(msgs[i], w);
            size_t offset = resp_buf.size();
            resp_buf.resize(offset + w.wire_size());
            std::memcpy(resp_buf.data() + offset, &w, WireMsg::header_size());
            std::memcpy(resp_buf.data() + offset + WireMsg::header_size(),
                        w.Data, w.DataSize);
        }

        // Update count (TX echos were skipped — but keep original for compat)
        send_response(pipe, status, resp_buf.data(),
                      static_cast<uint32_t>(resp_buf.size()));
        break;
    }

    // --- PassThruWriteMsgs ---
    case FUNC_WRITE_MSGS: {
        WriteMsgsRequest req{};
        if (hdr.payload_size < sizeof(req)) {
            send_response(pipe, 0x04 /*ERR_NULL_PARAMETER*/, nullptr, 0);
            break;
        }
        std::memcpy(&req, payload.data(), sizeof(req));

        uint32_t count = req.num_msgs;
        if (count > 64) count = 64;

        // Deserialize WireMsg array from payload
        std::vector<j2534::PASSTHRU_MSG> msgs(count);
        const uint8_t* p = payload.data() + sizeof(req);
        uint32_t bytes_left = hdr.payload_size - sizeof(req);

        for (uint32_t i = 0; i < count; ++i) {
            if (bytes_left < WireMsg::header_size()) break;

            WireMsg w{};
            std::memcpy(&w, p, WireMsg::header_size());
            p += WireMsg::header_size();
            bytes_left -= WireMsg::header_size();

            uint32_t data_len = (w.DataSize <= sizeof(w.Data)) ? w.DataSize : sizeof(w.Data);
            if (bytes_left < data_len) break;
            std::memcpy(w.Data, p, data_len);
            p += data_len;
            bytes_left -= data_len;

            wire_to_passthru(w, msgs[i]);
            emit_frame(msgs[i], true);
        }

        unsigned long num_written = count;
        long status = loader.PassThruWriteMsgs(
            req.channel_id, msgs.data(), &num_written, req.timeout);

        uint32_t resp_val = num_written;
        send_response(pipe, status, &resp_val, sizeof(resp_val));
        break;
    }

    // --- PassThruStartMsgFilter ---
    case FUNC_START_MSG_FILTER: {
        StartFilterRequest req{};
        if (hdr.payload_size < sizeof(req)) {
            send_response(pipe, 0x04, nullptr, 0);
            break;
        }
        std::memcpy(&req, payload.data(), sizeof(req));

        // Deserialize optional filter messages
        const uint8_t* p = payload.data() + sizeof(req);
        uint32_t bytes_left = hdr.payload_size - sizeof(req);

        auto read_wire_msg = [&](j2534::PASSTHRU_MSG& m) -> bool {
            if (bytes_left < WireMsg::header_size()) return false;
            WireMsg w{};
            std::memcpy(&w, p, WireMsg::header_size());
            p += WireMsg::header_size();
            bytes_left -= WireMsg::header_size();
            uint32_t dl = (w.DataSize <= sizeof(w.Data)) ? w.DataSize : sizeof(w.Data);
            if (bytes_left < dl) return false;
            std::memcpy(w.Data, p, dl);
            p += dl;
            bytes_left -= dl;
            wire_to_passthru(w, m);
            return true;
        };

        j2534::PASSTHRU_MSG mask{}, pattern{}, flow{};
        const j2534::PASSTHRU_MSG* p_mask = nullptr;
        const j2534::PASSTHRU_MSG* p_patt = nullptr;
        const j2534::PASSTHRU_MSG* p_flow = nullptr;

        if (req.has_mask && read_wire_msg(mask))       p_mask = &mask;
        if (req.has_pattern && read_wire_msg(pattern)) p_patt = &pattern;
        if (req.has_flow_control && read_wire_msg(flow)) p_flow = &flow;

        unsigned long filter_id = 0;
        long status = loader.PassThruStartMsgFilter(
            req.channel_id, req.filter_type, p_mask, p_patt, p_flow, &filter_id);

        StartFilterResponse resp{};
        resp.filter_id = filter_id;
        send_response(pipe, status, &resp, sizeof(resp));
        break;
    }

    // --- PassThruStopMsgFilter ---
    case FUNC_STOP_MSG_FILTER: {
        StopFilterRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        long status = loader.PassThruStopMsgFilter(req.channel_id, req.filter_id);
        send_response(pipe, status, nullptr, 0);
        break;
    }

    // --- PassThruStartPeriodicMsg ---
    case FUNC_START_PERIODIC: {
        StartPeriodicRequest req{};
        if (hdr.payload_size < sizeof(req)) {
            send_response(pipe, 0x04, nullptr, 0);
            break;
        }
        std::memcpy(&req, payload.data(), sizeof(req));

        j2534::PASSTHRU_MSG msg{};
        const uint8_t* p = payload.data() + sizeof(req);
        uint32_t bytes_left = hdr.payload_size - sizeof(req);
        if (bytes_left >= WireMsg::header_size()) {
            WireMsg w{};
            std::memcpy(&w, p, WireMsg::header_size());
            uint32_t dl = (w.DataSize <= sizeof(w.Data)) ? w.DataSize : sizeof(w.Data);
            if (bytes_left >= WireMsg::header_size() + dl) {
                std::memcpy(w.Data, p + WireMsg::header_size(), dl);
                wire_to_passthru(w, msg);
            }
        }

        unsigned long msg_id = 0;
        long status = loader.PassThruStartPeriodicMsg(
            req.channel_id, &msg, &msg_id, req.time_interval);

        StartPeriodicResponse resp{};
        resp.msg_id = msg_id;
        send_response(pipe, status, &resp, sizeof(resp));
        break;
    }

    // --- PassThruStopPeriodicMsg ---
    case FUNC_STOP_PERIODIC: {
        StopPeriodicRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        long status = loader.PassThruStopPeriodicMsg(req.channel_id, req.msg_id);
        send_response(pipe, status, nullptr, 0);
        break;
    }

    // --- PassThruIoctl ---
    case FUNC_IOCTL: {
        IoctlRequest req{};
        if (hdr.payload_size < sizeof(req)) {
            send_response(pipe, 0x04, nullptr, 0);
            break;
        }
        std::memcpy(&req, payload.data(), sizeof(req));

        // For SCONFIG_LIST-based IOCTLs, deserialize and forward
        void* p_input = nullptr;
        void* p_output = nullptr;

        // Read battery voltage
        if (req.ioctl_id == j2534::IOCTL_READ_VBATT) {
            unsigned long vbatt = 0;
            long status = loader.PassThruIoctl(req.channel_id, req.ioctl_id,
                                               nullptr, &vbatt);
            send_response(pipe, status, &vbatt, sizeof(vbatt));
            break;
        }

        // GET_CONFIG / SET_CONFIG: rebuild SCONFIG_LIST from wire data
        if ((req.ioctl_id == j2534::IOCTL_GET_CONFIG ||
             req.ioctl_id == j2534::IOCTL_SET_CONFIG) &&
            req.input_size > 0) {

            const uint8_t* cfg_data = payload.data() + sizeof(req);
            uint32_t cfg_bytes = req.input_size;

            // Wire format: [NumOfParams:u32] [Parameter:u32 Value:u32] × N
            if (cfg_bytes >= sizeof(uint32_t)) {
                uint32_t num_params = 0;
                std::memcpy(&num_params, cfg_data, sizeof(num_params));
                if (num_params > 100) num_params = 100;

                std::vector<j2534::SCONFIG> configs(num_params);
                const uint8_t* cp = cfg_data + sizeof(uint32_t);
                for (uint32_t i = 0; i < num_params; ++i) {
                    if (cp + sizeof(j2534::SCONFIG) <= cfg_data + cfg_bytes) {
                        std::memcpy(&configs[i], cp, sizeof(j2534::SCONFIG));
                        cp += sizeof(j2534::SCONFIG);
                    }
                }

                j2534::SCONFIG_LIST list{};
                list.NumOfParams = num_params;
                list.ConfigPtr = configs.data();

                long status = loader.PassThruIoctl(
                    req.channel_id, req.ioctl_id, &list, nullptr);

                // Send back the (possibly updated) config values
                std::vector<uint8_t> resp_buf(sizeof(uint32_t) + num_params * sizeof(j2534::SCONFIG));
                std::memcpy(resp_buf.data(), &num_params, sizeof(num_params));
                std::memcpy(resp_buf.data() + sizeof(uint32_t),
                           configs.data(), num_params * sizeof(j2534::SCONFIG));
                send_response(pipe, status, resp_buf.data(),
                             static_cast<uint32_t>(resp_buf.size()));
                break;
            }
        }

        // Other IOCTLs (clear buffers, etc.)
        long status = loader.PassThruIoctl(req.channel_id, req.ioctl_id,
                                           p_input, p_output);
        send_response(pipe, status, nullptr, 0);
        break;
    }

    // --- PassThruReadVersion ---
    case FUNC_READ_VERSION: {
        ReadVersionRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        ReadVersionResponse resp{};
        std::memset(&resp, 0, sizeof(resp));
        long status = loader.PassThruReadVersion(
            req.device_id, resp.firmware, resp.dll_version, resp.api_version);
        send_response(pipe, status, &resp, sizeof(resp));
        break;
    }

    // --- PassThruGetLastError ---
    case FUNC_GET_LAST_ERROR: {
        GetLastErrorResponse resp{};
        std::memset(&resp, 0, sizeof(resp));
        long status = loader.PassThruGetLastError(resp.description);
        send_response(pipe, status, &resp, sizeof(resp));
        break;
    }

    default:
        send_response(pipe, 0x01 /*ERR_NOT_SUPPORTED*/, nullptr, 0);
        break;
    }
}

// ---------------------------------------------------------------------------
// Dispatch (terminated mode): handle J2534 calls locally, emit frames
// ---------------------------------------------------------------------------
void ProxyServer::dispatch_terminated(HANDLE pipe, const ProxyRequest& hdr,
                                      const std::vector<uint8_t>& payload) {
    constexpr long OK  = 0x00;
    constexpr long ERR_DEVICE_IN_USE     = 0x0E;
    constexpr long ERR_INVALID_DEVICE_ID = 0x1A;
    constexpr long ERR_EXCEEDED_LIMIT    = 0x0C;
    constexpr long ERR_BUFFER_EMPTY      = 0x10;

    constexpr unsigned long FAKE_DEVICE_ID  = 1;
    constexpr unsigned long FAKE_CHANNEL_ID = 1;

    switch (static_cast<FuncId>(hdr.func_id)) {

    case FUNC_OPEN: {
        OpenRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        LOG_INFO("ProxyServer [terminated]: PassThruOpen(\"{}\")",
                 req.name[0] ? req.name : "(null)");

        if (term_state_.device_open) {
            send_response(pipe, ERR_DEVICE_IN_USE, nullptr, 0);
            break;
        }
        term_state_.device_open = true;

        OpenResponse resp{};
        resp.device_id = FAKE_DEVICE_ID;
        send_response(pipe, OK, &resp, sizeof(resp));
        break;
    }

    case FUNC_CLOSE: {
        CloseRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        LOG_INFO("ProxyServer [terminated]: PassThruClose({})", req.device_id);

        term_state_.device_open = false;
        term_state_.channel_open = false;
        send_response(pipe, OK, nullptr, 0);
        break;
    }

    case FUNC_CONNECT: {
        ConnectRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        const char* proto = (req.protocol_id == 0x05) ? "CAN"
                          : (req.protocol_id == 0x06) ? "ISO15765"
                          : "UNKNOWN";
        LOG_INFO("ProxyServer [terminated]: PassThruConnect(dev={}, proto={}({}), flags=0x{:08X}, baud={})",
                 req.device_id, proto, req.protocol_id, req.flags, req.baudrate);

        if (!term_state_.device_open) {
            send_response(pipe, ERR_INVALID_DEVICE_ID, nullptr, 0);
            break;
        }
        if (term_state_.channel_open) {
            send_response(pipe, ERR_EXCEEDED_LIMIT, nullptr, 0);
            break;
        }

        term_state_.channel_open = true;
        term_state_.protocol = req.protocol_id;
        term_state_.baudrate = req.baudrate;

        ConnectResponse resp{};
        resp.channel_id = FAKE_CHANNEL_ID;
        send_response(pipe, OK, &resp, sizeof(resp));
        break;
    }

    case FUNC_DISCONNECT: {
        DisconnectRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        LOG_INFO("ProxyServer [terminated]: PassThruDisconnect(ch={})", req.channel_id);
        term_state_.channel_open = false;
        send_response(pipe, OK, nullptr, 0);
        break;
    }

    case FUNC_READ_MSGS: {
        ReadMsgsRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        // Terminated bus: no ECU responses — sleep for timeout then return empty
        if (req.timeout > 0)
            Sleep(req.timeout);

        uint32_t num_msgs = 0;
        std::vector<uint8_t> resp_buf(sizeof(uint32_t));
        std::memcpy(resp_buf.data(), &num_msgs, sizeof(num_msgs));
        send_response(pipe, ERR_BUFFER_EMPTY, resp_buf.data(),
                      static_cast<uint32_t>(resp_buf.size()));
        break;
    }

    case FUNC_WRITE_MSGS: {
        WriteMsgsRequest req{};
        if (hdr.payload_size < sizeof(req)) {
            send_response(pipe, 0x04, nullptr, 0);
            break;
        }
        std::memcpy(&req, payload.data(), sizeof(req));

        uint32_t count = req.num_msgs;
        if (count > 64) count = 64;

        // Deserialize and emit each frame for GUI display
        const uint8_t* p = payload.data() + sizeof(req);
        uint32_t bytes_left = hdr.payload_size - sizeof(req);

        LOG_INFO("ProxyServer [terminated]: PassThruWriteMsgs(ch={}, n={})",
                 req.channel_id, count);

        for (uint32_t i = 0; i < count; ++i) {
            if (bytes_left < WireMsg::header_size()) break;

            WireMsg w{};
            std::memcpy(&w, p, WireMsg::header_size());
            p += WireMsg::header_size();
            bytes_left -= WireMsg::header_size();

            uint32_t data_len = (w.DataSize <= sizeof(w.Data)) ? w.DataSize : sizeof(w.Data);
            if (bytes_left < data_len) break;
            std::memcpy(w.Data, p, data_len);
            p += data_len;
            bytes_left -= data_len;

            j2534::PASSTHRU_MSG msg{};
            wire_to_passthru(w, msg);
            emit_frame(msg, true);
        }

        uint32_t num_written = count;
        send_response(pipe, OK, &num_written, sizeof(num_written));
        break;
    }

    case FUNC_START_MSG_FILTER: {
        StartFilterRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        unsigned long filter_id = term_state_.next_filter++;
        LOG_INFO("ProxyServer [terminated]: PassThruStartMsgFilter(ch={}, type={}) -> fid={}",
                 req.channel_id, req.filter_type, filter_id);

        StartFilterResponse resp{};
        resp.filter_id = filter_id;
        send_response(pipe, OK, &resp, sizeof(resp));
        break;
    }

    case FUNC_STOP_MSG_FILTER: {
        StopFilterRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        LOG_INFO("ProxyServer [terminated]: PassThruStopMsgFilter(ch={}, fid={})",
                 req.channel_id, req.filter_id);
        send_response(pipe, OK, nullptr, 0);
        break;
    }

    case FUNC_START_PERIODIC: {
        StartPeriodicRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        unsigned long msg_id = term_state_.next_periodic++;
        LOG_INFO("ProxyServer [terminated]: PassThruStartPeriodicMsg(ch={}, interval={}) -> mid={}",
                 req.channel_id, req.time_interval, msg_id);

        // Emit the periodic message frame for display
        const uint8_t* p = payload.data() + sizeof(req);
        uint32_t bytes_left = hdr.payload_size - sizeof(req);
        if (bytes_left >= WireMsg::header_size()) {
            WireMsg w{};
            std::memcpy(&w, p, WireMsg::header_size());
            uint32_t dl = (w.DataSize <= sizeof(w.Data)) ? w.DataSize : sizeof(w.Data);
            if (bytes_left >= WireMsg::header_size() + dl) {
                std::memcpy(w.Data, p + WireMsg::header_size(), dl);
                j2534::PASSTHRU_MSG msg{};
                wire_to_passthru(w, msg);
                emit_frame(msg, true);
            }
        }

        StartPeriodicResponse resp{};
        resp.msg_id = msg_id;
        send_response(pipe, OK, &resp, sizeof(resp));
        break;
    }

    case FUNC_STOP_PERIODIC: {
        StopPeriodicRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        LOG_INFO("ProxyServer [terminated]: PassThruStopPeriodicMsg(ch={}, mid={})",
                 req.channel_id, req.msg_id);
        send_response(pipe, OK, nullptr, 0);
        break;
    }

    case FUNC_IOCTL: {
        IoctlRequest req{};
        if (hdr.payload_size < sizeof(req)) {
            send_response(pipe, 0x04, nullptr, 0);
            break;
        }
        std::memcpy(&req, payload.data(), sizeof(req));

        const char* ioctl_name = "UNKNOWN";
        switch (req.ioctl_id) {
            case 0x01: ioctl_name = "GET_CONFIG"; break;
            case 0x02: ioctl_name = "SET_CONFIG"; break;
            case 0x03: ioctl_name = "READ_VBATT"; break;
            case 0x07: ioctl_name = "CLEAR_TX_BUFFER"; break;
            case 0x08: ioctl_name = "CLEAR_RX_BUFFER"; break;
            case 0x09: ioctl_name = "CLEAR_PERIODIC_MSGS"; break;
            case 0x0A: ioctl_name = "CLEAR_MSG_FILTERS"; break;
        }
        LOG_INFO("ProxyServer [terminated]: PassThruIoctl(ch={}, ioctl={}({}))",
                 req.channel_id, ioctl_name, req.ioctl_id);

        // READ_VBATT: return simulated 12.1 V
        if (req.ioctl_id == 0x03 /*READ_VBATT*/) {
            unsigned long vbatt = 12100;
            send_response(pipe, OK, &vbatt, sizeof(vbatt));
            break;
        }

        // GET_CONFIG: echo back the parameters with zero values
        if (req.ioctl_id == 0x01 /*GET_CONFIG*/ && req.input_size > 0) {
            const uint8_t* cfg_data = payload.data() + sizeof(req);
            uint32_t cfg_bytes = req.input_size;
            if (cfg_bytes >= sizeof(uint32_t)) {
                uint32_t num_params = 0;
                std::memcpy(&num_params, cfg_data, sizeof(num_params));
                if (num_params > 100) num_params = 100;

                std::vector<j2534::SCONFIG> configs(num_params);
                const uint8_t* cp = cfg_data + sizeof(uint32_t);
                for (uint32_t i = 0; i < num_params; ++i) {
                    if (cp + sizeof(j2534::SCONFIG) <= cfg_data + cfg_bytes) {
                        std::memcpy(&configs[i], cp, sizeof(j2534::SCONFIG));
                        cp += sizeof(j2534::SCONFIG);
                    }
                    // Return baudrate for DATA_RATE param, 0 for others
                    if (configs[i].Parameter == 0x01)
                        configs[i].Value = term_state_.baudrate;
                    else
                        configs[i].Value = 0;
                }

                std::vector<uint8_t> resp_buf(sizeof(uint32_t) + num_params * sizeof(j2534::SCONFIG));
                std::memcpy(resp_buf.data(), &num_params, sizeof(num_params));
                std::memcpy(resp_buf.data() + sizeof(uint32_t),
                           configs.data(), num_params * sizeof(j2534::SCONFIG));
                send_response(pipe, OK, resp_buf.data(),
                             static_cast<uint32_t>(resp_buf.size()));
                break;
            }
        }

        // SET_CONFIG, clears: accept silently
        send_response(pipe, OK, nullptr, 0);
        break;
    }

    case FUNC_READ_VERSION: {
        ReadVersionRequest req{};
        if (hdr.payload_size >= sizeof(req))
            std::memcpy(&req, payload.data(), sizeof(req));

        LOG_INFO("ProxyServer [terminated]: PassThruReadVersion(dev={})", req.device_id);

        ReadVersionResponse resp{};
        std::memset(&resp, 0, sizeof(resp));
        std::strncpy(resp.firmware, "1.00", sizeof(resp.firmware) - 1);
        std::strncpy(resp.dll_version, "1.00", sizeof(resp.dll_version) - 1);
        std::strncpy(resp.api_version, "04.04", sizeof(resp.api_version) - 1);
        send_response(pipe, OK, &resp, sizeof(resp));
        break;
    }

    case FUNC_GET_LAST_ERROR: {
        GetLastErrorResponse resp{};
        std::memset(&resp, 0, sizeof(resp));
        std::strncpy(resp.description, term_state_.last_error,
                     sizeof(resp.description) - 1);
        send_response(pipe, OK, &resp, sizeof(resp));
        break;
    }

    default:
        LOG_WARNING("ProxyServer [terminated]: unknown func_id={}", hdr.func_id);
        send_response(pipe, 0x01 /*ERR_NOT_SUPPORTED*/, nullptr, 0);
        break;
    }
}

} // namespace canmatik
