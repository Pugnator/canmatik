/// @file fake_j2534.cpp
/// Fake J2534 Pass-Thru DLL — proxy client mode.
///
/// When loaded by an external scan tool it connects to the CANmatik proxy
/// server via a named pipe and forwards every J2534 API call.  If the
/// server is not running, calls fall back to the original local-only
/// behavior (empty bus, logging only).
///
/// Exports the 13 mandatory J2534-1 functions (__stdcall).
/// Build: shared library (DLL), 32-bit.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

// ============================================================================
// J2534 Constants (self-contained — no dependency on main canmatik headers)
// ============================================================================
#define J2534_OK                     0x00
#define J2534_ERR_NOT_SUPPORTED      0x01
#define J2534_ERR_INVALID_CHANNEL_ID 0x02
#define J2534_ERR_INVALID_PROTOCOL   0x03
#define J2534_ERR_NULL_PARAMETER     0x04
#define J2534_ERR_INVALID_IOCTL      0x05
#define J2534_ERR_INVALID_FLAGS      0x06
#define J2534_ERR_FAILED             0x07
#define J2534_ERR_DEVICE_NOT_CONN    0x08
#define J2534_ERR_TIMEOUT            0x09
#define J2534_ERR_INVALID_MSG        0x0A
#define J2534_ERR_EXCEEDED_LIMIT     0x0C
#define J2534_ERR_INVALID_MSG_ID     0x0D
#define J2534_ERR_DEVICE_IN_USE      0x0E
#define J2534_ERR_BUFFER_EMPTY       0x10
#define J2534_ERR_INVALID_FILTER_ID  0x16
#define J2534_ERR_INVALID_BAUDRATE   0x19
#define J2534_ERR_INVALID_DEVICE_ID  0x1A

#define PROTOCOL_CAN       0x05
#define PROTOCOL_ISO15765  0x06

#define IOCTL_GET_CONFIG             0x01
#define IOCTL_SET_CONFIG             0x02
#define IOCTL_READ_VBATT             0x03
#define IOCTL_CLEAR_TX_BUFFER        0x07
#define IOCTL_CLEAR_RX_BUFFER        0x08
#define IOCTL_CLEAR_PERIODIC_MSGS    0x09
#define IOCTL_CLEAR_MSG_FILTERS      0x0A

#define FILTER_PASS          0x01
#define FILTER_BLOCK         0x02
#define FILTER_FLOW_CONTROL  0x03

#define CAN_29BIT_ID   0x00000100
#define CAN_ID_BOTH    0x00000800

#define RX_MSG_TX_DONE 0x00000008

constexpr size_t MSG_DATA_SIZE = 4128;

#pragma pack(push, 1)
struct PASSTHRU_MSG {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[MSG_DATA_SIZE];
};
#pragma pack(pop)

struct SCONFIG {
    unsigned long Parameter;
    unsigned long Value;
};

struct SCONFIG_LIST {
    unsigned long NumOfParams;
    SCONFIG* ConfigPtr;
};

// ============================================================================
// Proxy protocol types (mirrors proxy_protocol.h, self-contained)
// ============================================================================
namespace proxy_wire {

constexpr const char* kPipeName = "\\\\.\\pipe\\canmatik_proxy";

enum FuncId : uint32_t {
    FUNC_OPEN              = 0x01,
    FUNC_CLOSE             = 0x02,
    FUNC_CONNECT           = 0x03,
    FUNC_DISCONNECT        = 0x04,
    FUNC_READ_MSGS         = 0x05,
    FUNC_WRITE_MSGS        = 0x06,
    FUNC_START_MSG_FILTER  = 0x07,
    FUNC_STOP_MSG_FILTER   = 0x08,
    FUNC_START_PERIODIC    = 0x09,
    FUNC_STOP_PERIODIC     = 0x0A,
    FUNC_IOCTL             = 0x0B,
    FUNC_READ_VERSION      = 0x0C,
    FUNC_GET_LAST_ERROR    = 0x0D,
};

#pragma pack(push, 1)

struct ProxyRequest {
    uint32_t func_id;
    uint32_t payload_size;
};

struct ProxyResponse {
    int32_t  status;
    uint32_t payload_size;
};

struct WireMsg {
    uint32_t ProtocolID;
    uint32_t RxStatus;
    uint32_t TxFlags;
    uint32_t Timestamp;
    uint32_t DataSize;
    uint32_t ExtraDataIndex;
    uint8_t  Data[4128];

    static constexpr uint32_t header_size() { return 6 * sizeof(uint32_t); }
    uint32_t wire_size() const { return header_size() + DataSize; }
};

struct OpenRequest      { char name[256]; };
struct OpenResponse     { uint32_t device_id; };
struct CloseRequest     { uint32_t device_id; };
struct ConnectRequest   { uint32_t device_id; uint32_t protocol_id; uint32_t flags; uint32_t baudrate; };
struct ConnectResponse  { uint32_t channel_id; };
struct DisconnectRequest { uint32_t channel_id; };
struct ReadMsgsRequest  { uint32_t channel_id; uint32_t max_msgs; uint32_t timeout; };
struct WriteMsgsRequest { uint32_t channel_id; uint32_t num_msgs; uint32_t timeout; };
struct StartFilterRequest { uint32_t channel_id; uint32_t filter_type; uint8_t has_mask; uint8_t has_pattern; uint8_t has_flow_control; uint8_t reserved; };
struct StartFilterResponse { uint32_t filter_id; };
struct StopFilterRequest { uint32_t channel_id; uint32_t filter_id; };
struct StartPeriodicRequest { uint32_t channel_id; uint32_t time_interval; };
struct StartPeriodicResponse { uint32_t msg_id; };
struct StopPeriodicRequest { uint32_t channel_id; uint32_t msg_id; };
struct IoctlRequest { uint32_t channel_id; uint32_t ioctl_id; uint32_t input_size; };
struct ReadVersionRequest { uint32_t device_id; };
struct ReadVersionResponse { char firmware[80]; char dll_version[80]; char api_version[80]; };
struct GetLastErrorResponse { char description[80]; };

#pragma pack(pop)
} // namespace proxy_wire

// ============================================================================
// Logging
// ============================================================================
namespace {

std::mutex g_log_mutex;
FILE*      g_log_file = nullptr;

void log_open() {
    if (g_log_file) return;

    // Log alongside the DLL itself
    char dll_path[MAX_PATH] = {};
    HMODULE hSelf = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&log_open),
        &hSelf);
    GetModuleFileNameA(hSelf, dll_path, MAX_PATH);

    // Replace .dll with .log
    std::string path(dll_path);
    auto dot = path.rfind('.');
    if (dot != std::string::npos) path = path.substr(0, dot);
    path += ".log";

    g_log_file = fopen(path.c_str(), "a");

    // Fallback: write to %TEMP%\<dllname>.log if SysWOW64 is not writable
    if (!g_log_file) {
        const char* tmp = getenv("TEMP");
        if (!tmp) tmp = getenv("TMP");
        if (tmp) {
            // Extract just the filename (without directory)
            std::string fname(dll_path);
            auto slash = fname.find_last_of("\\/");
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            auto d = fname.rfind('.');
            if (d != std::string::npos) fname = fname.substr(0, d);
            fname += ".log";

            path = std::string(tmp) + "\\" + fname;
            g_log_file = fopen(path.c_str(), "a");
        }
    }
}

void log_close() {
    if (g_log_file) { fclose(g_log_file); g_log_file = nullptr; }
}

void j2534_log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    if (!g_log_file) return;

    // Timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap);
    va_end(ap);

    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

const char* protocol_name(unsigned long id) {
    switch (id) {
        case PROTOCOL_CAN:      return "CAN";
        case PROTOCOL_ISO15765: return "ISO15765";
        default:                return "UNKNOWN";
    }
}

const char* ioctl_name(unsigned long id) {
    switch (id) {
        case IOCTL_GET_CONFIG:          return "GET_CONFIG";
        case IOCTL_SET_CONFIG:          return "SET_CONFIG";
        case IOCTL_READ_VBATT:          return "READ_VBATT";
        case IOCTL_CLEAR_TX_BUFFER:     return "CLEAR_TX_BUFFER";
        case IOCTL_CLEAR_RX_BUFFER:     return "CLEAR_RX_BUFFER";
        case IOCTL_CLEAR_PERIODIC_MSGS: return "CLEAR_PERIODIC_MSGS";
        case IOCTL_CLEAR_MSG_FILTERS:   return "CLEAR_MSG_FILTERS";
        default:                        return "UNKNOWN_IOCTL";
    }
}

void log_msg(const char* prefix, const PASSTHRU_MSG* msg) {
    if (!msg) return;
    j2534_log("  %s: Proto=%s(%lu) DataSize=%lu RxStatus=0x%08lX TxFlags=0x%08lX",
              prefix, protocol_name(msg->ProtocolID), msg->ProtocolID,
              msg->DataSize, msg->RxStatus, msg->TxFlags);
    if (msg->DataSize > 0 && msg->DataSize <= 16) {
        char hex[128] = {};
        for (unsigned long i = 0; i < msg->DataSize; ++i)
            sprintf(hex + i * 3, "%02X ", msg->Data[i]);
        j2534_log("    Data: %s", hex);
    }
}

// ============================================================================
// Pipe client — send request, receive response
// ============================================================================
HANDLE g_pipe = INVALID_HANDLE_VALUE;

bool pipe_read_exact(void* buf, uint32_t len) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    uint32_t remaining = len;
    while (remaining > 0) {
        DWORD got = 0;
        if (!ReadFile(g_pipe, p, remaining, &got, nullptr) || got == 0)
            return false;
        p += got;
        remaining -= got;
    }
    return true;
}

bool pipe_write_exact(const void* buf, uint32_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    uint32_t remaining = len;
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(g_pipe, p, remaining, &written, nullptr) || written == 0)
            return false;
        p += written;
        remaining -= written;
    }
    return true;
}

bool pipe_connect() {
    if (g_pipe != INVALID_HANDLE_VALUE) return true;

    g_pipe = CreateFileA(proxy_wire::kPipeName,
                         GENERIC_READ | GENERIC_WRITE,
                         0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (g_pipe == INVALID_HANDLE_VALUE) {
        j2534_log("  pipe: cannot connect to proxy server (err=%lu)", GetLastError());
        return false;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(g_pipe, &mode, nullptr, nullptr);
    j2534_log("  pipe: connected to proxy server");
    return true;
}

void pipe_disconnect() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

bool is_proxy_connected() {
    return g_pipe != INVALID_HANDLE_VALUE;
}

/// Send a request and read the response header + payload.
/// Returns false if the pipe is broken.
bool proxy_call(uint32_t func_id, const void* req_payload, uint32_t req_size,
                proxy_wire::ProxyResponse& resp_hdr, std::vector<uint8_t>& resp_payload) {
    if (!is_proxy_connected()) return false;

    proxy_wire::ProxyRequest hdr{};
    hdr.func_id = func_id;
    hdr.payload_size = req_size;

    if (!pipe_write_exact(&hdr, sizeof(hdr))) { pipe_disconnect(); return false; }
    if (req_size > 0 && req_payload) {
        if (!pipe_write_exact(req_payload, req_size)) { pipe_disconnect(); return false; }
    }
    FlushFileBuffers(g_pipe);

    if (!pipe_read_exact(&resp_hdr, sizeof(resp_hdr))) { pipe_disconnect(); return false; }
    resp_payload.clear();
    if (resp_hdr.payload_size > 0) {
        if (resp_hdr.payload_size > 1024 * 1024) { pipe_disconnect(); return false; }
        resp_payload.resize(resp_hdr.payload_size);
        if (!pipe_read_exact(resp_payload.data(), resp_hdr.payload_size)) {
            pipe_disconnect(); return false;
        }
    }
    return true;
}

// ============================================================================
// Device / Channel state (fallback when proxy not running)
// ============================================================================
constexpr unsigned long FAKE_DEVICE_ID  = 1;
constexpr unsigned long FAKE_CHANNEL_ID = 1;

struct FakeState {
    bool device_open   = false;
    bool channel_open  = false;
    unsigned long protocol     = 0;
    unsigned long baudrate     = 0;
    unsigned long flags        = 0;
    unsigned long next_filter  = 1;
    unsigned long filter_count = 0;
    char last_error[256]       = {};
};

std::mutex  g_state_mutex;
FakeState   g_state;

void set_last_error(const char* msg) {
    strncpy(g_state.last_error, msg, sizeof(g_state.last_error) - 1);
    g_state.last_error[sizeof(g_state.last_error) - 1] = '\0';
}

// ============================================================================
// Helper: convert PASSTHRU_MSG <-> WireMsg
// ============================================================================
void passthru_to_wire(const PASSTHRU_MSG& m, proxy_wire::WireMsg& w) {
    w.ProtocolID     = m.ProtocolID;
    w.RxStatus       = m.RxStatus;
    w.TxFlags        = m.TxFlags;
    w.Timestamp      = m.Timestamp;
    w.DataSize       = m.DataSize;
    w.ExtraDataIndex = m.ExtraDataIndex;
    uint32_t copy_len = (m.DataSize <= sizeof(w.Data)) ? m.DataSize : sizeof(w.Data);
    memcpy(w.Data, m.Data, copy_len);
}

void wire_to_passthru(const proxy_wire::WireMsg& w, PASSTHRU_MSG& m) {
    memset(&m, 0, sizeof(m));
    m.ProtocolID     = w.ProtocolID;
    m.RxStatus       = w.RxStatus;
    m.TxFlags        = w.TxFlags;
    m.Timestamp      = w.Timestamp;
    m.DataSize       = w.DataSize;
    m.ExtraDataIndex = w.ExtraDataIndex;
    uint32_t copy_len = (w.DataSize <= MSG_DATA_SIZE) ? w.DataSize : MSG_DATA_SIZE;
    memcpy(m.Data, w.Data, copy_len);
}

} // anonymous namespace

// ============================================================================
// DLL entry point
// ============================================================================
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            log_open();
            j2534_log("=== DLL_PROCESS_ATTACH (PID %lu) ===", GetCurrentProcessId());
            break;
        case DLL_PROCESS_DETACH:
            j2534_log("=== DLL_PROCESS_DETACH ===");
            pipe_disconnect();
            log_close();
            break;
    }
    return TRUE;
}

// ============================================================================
// J2534 API exports
// ============================================================================
#define EXPORT extern "C" __declspec(dllexport) long __stdcall

EXPORT PassThruOpen(void* pName, unsigned long* pDeviceID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);

    const char* name = pName ? static_cast<const char*>(pName) : "(null)";
    j2534_log("PassThruOpen(pName=\"%s\")", name);

    if (!pDeviceID) {
        set_last_error("pDeviceID is NULL");
        j2534_log("  -> ERR_NULL_PARAMETER");
        return J2534_ERR_NULL_PARAMETER;
    }

    // Try proxy mode first
    if (pipe_connect()) {
        proxy_wire::OpenRequest req{};
        if (pName) strncpy(req.name, static_cast<const char*>(pName), sizeof(req.name) - 1);

        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_OPEN, &req, sizeof(req), resp_hdr, resp_data)) {
            if (resp_hdr.status == J2534_OK && resp_data.size() >= sizeof(proxy_wire::OpenResponse)) {
                proxy_wire::OpenResponse resp{};
                memcpy(&resp, resp_data.data(), sizeof(resp));
                *pDeviceID = resp.device_id;
                g_state.device_open = true;
            }
            j2534_log("  -> proxy: status=%ld DeviceID=%lu", resp_hdr.status, *pDeviceID);
            return resp_hdr.status;
        }
    }

    // Fallback: local fake mode
    if (g_state.device_open) {
        set_last_error("Device already open");
        j2534_log("  -> ERR_DEVICE_IN_USE (local)");
        return J2534_ERR_DEVICE_IN_USE;
    }

    g_state = FakeState{};
    g_state.device_open = true;
    *pDeviceID = FAKE_DEVICE_ID;

    j2534_log("  -> OK (local), DeviceID=%lu", *pDeviceID);
    return J2534_OK;
}

EXPORT PassThruClose(unsigned long DeviceID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruClose(DeviceID=%lu)", DeviceID);

    if (is_proxy_connected()) {
        proxy_wire::CloseRequest req{};
        req.device_id = DeviceID;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_CLOSE, &req, sizeof(req), resp_hdr, resp_data)) {
            g_state.device_open = false;
            g_state.channel_open = false;
            pipe_disconnect();
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    if (DeviceID != FAKE_DEVICE_ID || !g_state.device_open) {
        set_last_error("Invalid device ID");
        j2534_log("  -> ERR_INVALID_DEVICE_ID");
        return J2534_ERR_INVALID_DEVICE_ID;
    }

    g_state.device_open = false;
    g_state.channel_open = false;
    j2534_log("  -> OK");
    return J2534_OK;
}

EXPORT PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID,
                       unsigned long Flags, unsigned long Baudrate,
                       unsigned long* pChannelID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruConnect(DeviceID=%lu, Protocol=%s(%lu), Flags=0x%08lX, Baudrate=%lu)",
              DeviceID, protocol_name(ProtocolID), ProtocolID, Flags, Baudrate);

    if (!pChannelID) {
        set_last_error("pChannelID is NULL");
        return J2534_ERR_NULL_PARAMETER;
    }

    if (is_proxy_connected()) {
        proxy_wire::ConnectRequest req{};
        req.device_id = DeviceID;
        req.protocol_id = ProtocolID;
        req.flags = Flags;
        req.baudrate = Baudrate;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_CONNECT, &req, sizeof(req), resp_hdr, resp_data)) {
            if (resp_hdr.status == J2534_OK && resp_data.size() >= sizeof(proxy_wire::ConnectResponse)) {
                proxy_wire::ConnectResponse resp{};
                memcpy(&resp, resp_data.data(), sizeof(resp));
                *pChannelID = resp.channel_id;
                g_state.channel_open = true;
                g_state.protocol = ProtocolID;
                g_state.baudrate = Baudrate;
            }
            j2534_log("  -> proxy: status=%ld ChannelID=%lu", resp_hdr.status, *pChannelID);
            return resp_hdr.status;
        }
    }

    // Fallback: local fake mode
    if (DeviceID != FAKE_DEVICE_ID || !g_state.device_open) return J2534_ERR_INVALID_DEVICE_ID;
    if (ProtocolID != PROTOCOL_CAN && ProtocolID != PROTOCOL_ISO15765) return J2534_ERR_INVALID_PROTOCOL;
    if (g_state.channel_open) return J2534_ERR_EXCEEDED_LIMIT;

    g_state.channel_open = true;
    g_state.protocol = ProtocolID;
    g_state.baudrate = Baudrate;
    g_state.flags = Flags;
    *pChannelID = FAKE_CHANNEL_ID;
    j2534_log("  -> OK (local), ChannelID=%lu", *pChannelID);
    return J2534_OK;
}

EXPORT PassThruDisconnect(unsigned long ChannelID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruDisconnect(ChannelID=%lu)", ChannelID);

    if (is_proxy_connected()) {
        proxy_wire::DisconnectRequest req{};
        req.channel_id = ChannelID;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_DISCONNECT, &req, sizeof(req), resp_hdr, resp_data)) {
            g_state.channel_open = false;
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    g_state.channel_open = false;
    j2534_log("  -> OK");
    return J2534_OK;
}

EXPORT PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG* pMsg,
                        unsigned long* pNumMsgs, unsigned long Timeout) {
    std::lock_guard<std::mutex> lk(g_state_mutex);

    unsigned long requested = (pNumMsgs ? *pNumMsgs : 0);
    j2534_log("PassThruReadMsgs(ChannelID=%lu, NumMsgs=%lu, Timeout=%lu)",
              ChannelID, requested, Timeout);

    if (!pMsg || !pNumMsgs) {
        set_last_error("NULL parameter");
        return J2534_ERR_NULL_PARAMETER;
    }

    if (is_proxy_connected()) {
        proxy_wire::ReadMsgsRequest req{};
        req.channel_id = ChannelID;
        req.max_msgs = requested;
        req.timeout = Timeout;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_READ_MSGS, &req, sizeof(req), resp_hdr, resp_data)) {
            // Parse response: [num_msgs:u32] [WireMsg × n (variable)]
            uint32_t num_msgs = 0;
            if (resp_data.size() >= sizeof(uint32_t))
                memcpy(&num_msgs, resp_data.data(), sizeof(num_msgs));

            const uint8_t* p = resp_data.data() + sizeof(uint32_t);
            uint32_t bytes_left = (resp_data.size() > sizeof(uint32_t))
                                ? static_cast<uint32_t>(resp_data.size() - sizeof(uint32_t)) : 0;

            uint32_t copied = 0;
            for (uint32_t i = 0; i < num_msgs && copied < requested; ++i) {
                if (bytes_left < proxy_wire::WireMsg::header_size()) break;
                proxy_wire::WireMsg w{};
                memcpy(&w, p, proxy_wire::WireMsg::header_size());
                p += proxy_wire::WireMsg::header_size();
                bytes_left -= proxy_wire::WireMsg::header_size();

                uint32_t dl = (w.DataSize <= sizeof(w.Data)) ? w.DataSize : sizeof(w.Data);
                if (bytes_left < dl) break;
                memcpy(w.Data, p, dl);
                p += dl;
                bytes_left -= dl;

                wire_to_passthru(w, pMsg[copied]);
                log_msg("RX", &pMsg[copied]);
                ++copied;
            }

            *pNumMsgs = copied;
            j2534_log("  -> proxy: status=%ld msgs=%lu", resp_hdr.status, copied);
            return resp_hdr.status;
        }
    }

    // Fallback: empty bus
    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    if (Timeout > 0) {
        g_state_mutex.unlock();
        Sleep(Timeout);
        g_state_mutex.lock();
    }
    *pNumMsgs = 0;
    j2534_log("  -> ERR_BUFFER_EMPTY (local, silent bus)");
    return J2534_ERR_BUFFER_EMPTY;
}

EXPORT PassThruWriteMsgs(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
                         unsigned long* pNumMsgs, unsigned long Timeout) {
    std::lock_guard<std::mutex> lk(g_state_mutex);

    unsigned long count = (pNumMsgs ? *pNumMsgs : 0);
    j2534_log("PassThruWriteMsgs(ChannelID=%lu, NumMsgs=%lu, Timeout=%lu)",
              ChannelID, count, Timeout);

    // Log each message
    for (unsigned long i = 0; i < count && pMsg; ++i) {
        char label[32];
        sprintf(label, "TX[%lu]", i);
        log_msg(label, &pMsg[i]);
    }

    if (is_proxy_connected()) {
        // Build payload: WriteMsgsRequest + WireMsg array
        proxy_wire::WriteMsgsRequest req{};
        req.channel_id = ChannelID;
        req.num_msgs = count;
        req.timeout = Timeout;

        std::vector<uint8_t> payload(sizeof(req));
        memcpy(payload.data(), &req, sizeof(req));

        for (unsigned long i = 0; i < count && pMsg; ++i) {
            proxy_wire::WireMsg w{};
            passthru_to_wire(pMsg[i], w);
            size_t offset = payload.size();
            payload.resize(offset + w.wire_size());
            memcpy(payload.data() + offset, &w, proxy_wire::WireMsg::header_size());
            memcpy(payload.data() + offset + proxy_wire::WireMsg::header_size(),
                   w.Data, w.DataSize);
        }

        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_WRITE_MSGS, payload.data(),
                       static_cast<uint32_t>(payload.size()), resp_hdr, resp_data)) {
            if (resp_data.size() >= sizeof(uint32_t) && pNumMsgs) {
                uint32_t num_written = 0;
                memcpy(&num_written, resp_data.data(), sizeof(num_written));
                *pNumMsgs = num_written;
            }
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    // Fallback: accept all
    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    j2534_log("  -> OK (local, accepted %lu msgs)", count);
    return J2534_OK;
}

EXPORT PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                               const PASSTHRU_MSG* pMaskMsg,
                               const PASSTHRU_MSG* pPatternMsg,
                               const PASSTHRU_MSG* pFlowControlMsg,
                               unsigned long* pMsgID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStartMsgFilter(ChannelID=%lu, FilterType=%lu)", ChannelID, FilterType);

    if (!pMsgID) {
        set_last_error("pMsgID is NULL");
        return J2534_ERR_NULL_PARAMETER;
    }

    log_msg("Mask", pMaskMsg);
    log_msg("Pattern", pPatternMsg);
    log_msg("FlowControl", pFlowControlMsg);

    if (is_proxy_connected()) {
        proxy_wire::StartFilterRequest req{};
        req.channel_id = ChannelID;
        req.filter_type = FilterType;
        req.has_mask = pMaskMsg ? 1 : 0;
        req.has_pattern = pPatternMsg ? 1 : 0;
        req.has_flow_control = pFlowControlMsg ? 1 : 0;

        std::vector<uint8_t> payload(sizeof(req));
        memcpy(payload.data(), &req, sizeof(req));

        auto append_wire_msg = [&](const PASSTHRU_MSG* msg) {
            if (!msg) return;
            proxy_wire::WireMsg w{};
            passthru_to_wire(*msg, w);
            size_t offset = payload.size();
            payload.resize(offset + w.wire_size());
            memcpy(payload.data() + offset, &w, proxy_wire::WireMsg::header_size());
            memcpy(payload.data() + offset + proxy_wire::WireMsg::header_size(),
                   w.Data, w.DataSize);
        };
        append_wire_msg(pMaskMsg);
        append_wire_msg(pPatternMsg);
        append_wire_msg(pFlowControlMsg);

        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_START_MSG_FILTER, payload.data(),
                       static_cast<uint32_t>(payload.size()), resp_hdr, resp_data)) {
            if (resp_hdr.status == J2534_OK && resp_data.size() >= sizeof(proxy_wire::StartFilterResponse)) {
                proxy_wire::StartFilterResponse resp{};
                memcpy(&resp, resp_data.data(), sizeof(resp));
                *pMsgID = resp.filter_id;
            }
            j2534_log("  -> proxy: status=%ld FilterID=%lu", resp_hdr.status, *pMsgID);
            return resp_hdr.status;
        }
    }

    // Fallback: local
    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    *pMsgID = g_state.next_filter++;
    g_state.filter_count++;
    j2534_log("  -> OK (local), FilterID=%lu", *pMsgID);
    return J2534_OK;
}

EXPORT PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStopMsgFilter(ChannelID=%lu, MsgID=%lu)", ChannelID, MsgID);

    if (is_proxy_connected()) {
        proxy_wire::StopFilterRequest req{};
        req.channel_id = ChannelID;
        req.filter_id = MsgID;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_STOP_MSG_FILTER, &req, sizeof(req), resp_hdr, resp_data)) {
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    if (g_state.filter_count > 0) g_state.filter_count--;
    j2534_log("  -> OK (local)");
    return J2534_OK;
}

EXPORT PassThruStartPeriodicMsg(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
                                unsigned long* pMsgID, unsigned long TimeInterval) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStartPeriodicMsg(ChannelID=%lu, Interval=%lu)", ChannelID, TimeInterval);
    log_msg("Periodic", pMsg);

    if (is_proxy_connected() && pMsg) {
        proxy_wire::StartPeriodicRequest req{};
        req.channel_id = ChannelID;
        req.time_interval = TimeInterval;

        proxy_wire::WireMsg w{};
        passthru_to_wire(*pMsg, w);

        std::vector<uint8_t> payload(sizeof(req) + w.wire_size());
        memcpy(payload.data(), &req, sizeof(req));
        memcpy(payload.data() + sizeof(req), &w, proxy_wire::WireMsg::header_size());
        memcpy(payload.data() + sizeof(req) + proxy_wire::WireMsg::header_size(),
               w.Data, w.DataSize);

        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_START_PERIODIC, payload.data(),
                       static_cast<uint32_t>(payload.size()), resp_hdr, resp_data)) {
            if (pMsgID && resp_data.size() >= sizeof(proxy_wire::StartPeriodicResponse)) {
                proxy_wire::StartPeriodicResponse resp{};
                memcpy(&resp, resp_data.data(), sizeof(resp));
                *pMsgID = resp.msg_id;
            }
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    if (pMsgID) *pMsgID = 1;
    j2534_log("  -> OK (local)");
    return J2534_OK;
}

EXPORT PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStopPeriodicMsg(ChannelID=%lu, MsgID=%lu)", ChannelID, MsgID);

    if (is_proxy_connected()) {
        proxy_wire::StopPeriodicRequest req{};
        req.channel_id = ChannelID;
        req.msg_id = MsgID;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_STOP_PERIODIC, &req, sizeof(req), resp_hdr, resp_data)) {
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) return J2534_ERR_INVALID_CHANNEL_ID;
    j2534_log("  -> OK (local)");
    return J2534_OK;
}

EXPORT PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                     void* pInput, void* pOutput) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruIoctl(ChannelID=%lu, Ioctl=%s(%lu))",
              ChannelID, ioctl_name(IoctlID), IoctlID);

    if (is_proxy_connected()) {
        // Serialize IOCTL request
        proxy_wire::IoctlRequest req{};
        req.channel_id = ChannelID;
        req.ioctl_id = IoctlID;
        req.input_size = 0;

        std::vector<uint8_t> payload(sizeof(req));

        // Serialize SCONFIG_LIST for GET/SET_CONFIG
        if ((IoctlID == IOCTL_GET_CONFIG || IoctlID == IOCTL_SET_CONFIG) && pInput) {
            auto* list = static_cast<SCONFIG_LIST*>(pInput);
            if (list->ConfigPtr && list->NumOfParams > 0) {
                uint32_t num = list->NumOfParams;
                uint32_t cfg_size = sizeof(uint32_t) + num * sizeof(SCONFIG);
                req.input_size = cfg_size;
                memcpy(payload.data(), &req, sizeof(req));
                payload.resize(sizeof(req) + cfg_size);
                memcpy(payload.data() + sizeof(req), &num, sizeof(num));
                memcpy(payload.data() + sizeof(req) + sizeof(num),
                       list->ConfigPtr, num * sizeof(SCONFIG));
            } else {
                memcpy(payload.data(), &req, sizeof(req));
            }
        } else {
            memcpy(payload.data(), &req, sizeof(req));
        }

        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_IOCTL, payload.data(),
                       static_cast<uint32_t>(payload.size()), resp_hdr, resp_data)) {
            // Handle response data
            if (IoctlID == IOCTL_READ_VBATT && pOutput && resp_data.size() >= sizeof(unsigned long)) {
                memcpy(pOutput, resp_data.data(), sizeof(unsigned long));
            }
            if ((IoctlID == IOCTL_GET_CONFIG) && pInput && resp_data.size() >= sizeof(uint32_t)) {
                auto* list = static_cast<SCONFIG_LIST*>(pInput);
                uint32_t num = 0;
                memcpy(&num, resp_data.data(), sizeof(num));
                if (list->ConfigPtr && num <= list->NumOfParams) {
                    memcpy(list->ConfigPtr, resp_data.data() + sizeof(uint32_t),
                           num * sizeof(SCONFIG));
                }
            }
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    // Fallback: local handling
    if (IoctlID == IOCTL_READ_VBATT) {
        if (pOutput) *static_cast<unsigned long*>(pOutput) = 12100;
        j2534_log("  -> OK (local), Vbatt=12100 mV");
        return J2534_OK;
    }

    if (ChannelID != FAKE_CHANNEL_ID && ChannelID != FAKE_DEVICE_ID) return J2534_ERR_INVALID_CHANNEL_ID;

    switch (IoctlID) {
        case IOCTL_GET_CONFIG: {
            auto* list = static_cast<SCONFIG_LIST*>(pInput);
            if (list && list->ConfigPtr) {
                for (unsigned long i = 0; i < list->NumOfParams; ++i) {
                    if (list->ConfigPtr[i].Parameter == 0x01)
                        list->ConfigPtr[i].Value = g_state.baudrate;
                    else
                        list->ConfigPtr[i].Value = 0;
                }
            }
            j2534_log("  -> OK (local)");
            return J2534_OK;
        }
        case IOCTL_SET_CONFIG:
            j2534_log("  -> OK (local)");
            return J2534_OK;
        case IOCTL_CLEAR_TX_BUFFER:
        case IOCTL_CLEAR_RX_BUFFER:
        case IOCTL_CLEAR_PERIODIC_MSGS:
        case IOCTL_CLEAR_MSG_FILTERS:
            g_state.filter_count = 0;
            j2534_log("  -> OK (local, cleared)");
            return J2534_OK;
        default:
            set_last_error("Unsupported IOCTL");
            j2534_log("  -> ERR_INVALID_IOCTL (local)");
            return J2534_ERR_INVALID_IOCTL;
    }
}

EXPORT PassThruReadVersion(unsigned long DeviceID, char* pFirmwareVersion,
                           char* pDllVersion, char* pApiVersion) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruReadVersion(DeviceID=%lu)", DeviceID);

    if (is_proxy_connected()) {
        proxy_wire::ReadVersionRequest req{};
        req.device_id = DeviceID;
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_READ_VERSION, &req, sizeof(req), resp_hdr, resp_data)) {
            if (resp_data.size() >= sizeof(proxy_wire::ReadVersionResponse)) {
                proxy_wire::ReadVersionResponse resp{};
                memcpy(&resp, resp_data.data(), sizeof(resp));
                if (pFirmwareVersion) strncpy(pFirmwareVersion, resp.firmware, 79);
                if (pDllVersion)      strncpy(pDllVersion, resp.dll_version, 79);
                if (pApiVersion)      strncpy(pApiVersion, resp.api_version, 79);
            }
            j2534_log("  -> proxy: status=%ld", resp_hdr.status);
            return resp_hdr.status;
        }
    }

    if (DeviceID != FAKE_DEVICE_ID || !g_state.device_open) return J2534_ERR_INVALID_DEVICE_ID;
    if (pFirmwareVersion) strcpy(pFirmwareVersion, "1.00");
    if (pDllVersion)      strcpy(pDllVersion,      "1.00");
    if (pApiVersion)      strcpy(pApiVersion,       "04.04");
    j2534_log("  -> OK (local, FW=1.00 DLL=1.00 API=04.04)");
    return J2534_OK;
}

EXPORT PassThruGetLastError(char* pErrorDescription) {
    std::lock_guard<std::mutex> lk(g_state_mutex);

    if (is_proxy_connected()) {
        proxy_wire::ProxyResponse resp_hdr{};
        std::vector<uint8_t> resp_data;
        if (proxy_call(proxy_wire::FUNC_GET_LAST_ERROR, nullptr, 0, resp_hdr, resp_data)) {
            if (pErrorDescription && resp_data.size() >= sizeof(proxy_wire::GetLastErrorResponse)) {
                proxy_wire::GetLastErrorResponse resp{};
                memcpy(&resp, resp_data.data(), sizeof(resp));
                strncpy(pErrorDescription, resp.description, 79);
                pErrorDescription[79] = '\0';
            }
            return resp_hdr.status;
        }
    }

    if (pErrorDescription) {
        strncpy(pErrorDescription, g_state.last_error, 79);
        pErrorDescription[79] = '\0';
    }
    return J2534_OK;
}

EXPORT PassThruSetProgrammingVoltage(unsigned long DeviceID,
                                     unsigned long PinNumber,
                                     unsigned long Voltage) {
    j2534_log("PassThruSetProgrammingVoltage(DeviceID=%lu, Pin=%lu, Voltage=%lu)",
              DeviceID, PinNumber, Voltage);
    // Not applicable for CAN-only proxy — accept but ignore.
    return J2534_OK;
}

// ============================================================================
// J2534-2 extension stubs
// Some tools (e.g. romdrop for Openport 2.0) call these during DLL init.
// ============================================================================

// SDEVICE structure for PassThruScanForDevices / PassThruGetNextDevice
#pragma pack(push, 1)
struct SDEVICE {
    char DeviceName[80];
    unsigned long DeviceAvailable;
    unsigned long DeviceDLLFWStatus;
    unsigned long DeviceConnectMedia;
    unsigned long DeviceConnectSpeed;
    unsigned long DeviceSignalQuality;
    unsigned long DeviceSignalStrength;
};
#pragma pack(pop)

EXPORT PassThruLoadLibrary(const char* LibraryPath) {
    j2534_log("PassThruLoadLibrary(LibraryPath=\"%s\")",
              LibraryPath ? LibraryPath : "(null)");
    // Nothing to load — we are the library. Return OK.
    return J2534_OK;
}

EXPORT PassThruUnloadLibrary(void) {
    j2534_log("PassThruUnloadLibrary()");
    return J2534_OK;
}

EXPORT PassThruScanForDevices(unsigned long* pDeviceCount) {
    j2534_log("PassThruScanForDevices()");
    if (pDeviceCount)
        *pDeviceCount = 1;  // report one device available
    return J2534_OK;
}

EXPORT PassThruGetNextDevice(SDEVICE* psDevice) {
    j2534_log("PassThruGetNextDevice()");
    if (!psDevice)
        return J2534_ERR_NULL_PARAMETER;
    memset(psDevice, 0, sizeof(SDEVICE));
    strncpy(psDevice->DeviceName, "CANmatik Proxy", sizeof(psDevice->DeviceName) - 1);
    psDevice->DeviceAvailable = 1;
    return J2534_OK;
}

EXPORT PassThruSelect(void* pSChannelSet, unsigned long SelectType, unsigned long Timeout) {
    j2534_log("PassThruSelect(SelectType=%lu, Timeout=%lu)", SelectType, Timeout);
    return J2534_OK;
}

EXPORT PassThruQueueMsgs(unsigned long ChannelID, const PASSTHRU_MSG* pMsg, unsigned long* pNumMsgs) {
    j2534_log("PassThruQueueMsgs(ChannelID=%lu)", ChannelID);
    // Forward to WriteMsgs with zero timeout
    unsigned long timeout = 0;
    return PassThruWriteMsgs(ChannelID, pMsg, pNumMsgs, timeout);
}
