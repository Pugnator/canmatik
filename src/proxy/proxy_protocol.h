#pragma once

/// @file proxy_protocol.h
/// Shared IPC protocol between the fake J2534 DLL (client) and the
/// CANmatik proxy server.  Both sides are 32-bit processes on the same
/// machine, so struct layout is guaranteed identical.
///
/// Wire protocol (named pipe, synchronous):
///   Client -> Server: ProxyRequest header, then payload bytes
///   Server -> Client: ProxyResponse header, then payload bytes

#include <cstdint>

namespace canmatik {
namespace proxy {

// Named pipe path (must match in fake DLL and proxy server)
constexpr const char* kPipeName = "\\\\.\\pipe\\canmatik_proxy";

// Pipe buffer sizes (bytes)
constexpr uint32_t kPipeBufferSize = 65536;

// ===========================================================================
// Function IDs
// ===========================================================================
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

// ===========================================================================
// Wire header (always first on the pipe)
// ===========================================================================
#pragma pack(push, 1)

struct ProxyRequest {
    uint32_t func_id;
    uint32_t payload_size;   // bytes following this header
};

struct ProxyResponse {
    int32_t  status;         // J2534 status code (long)
    uint32_t payload_size;   // bytes following this header
};

// ===========================================================================
// Compact CAN message for wire transfer.
// Smaller than PASSTHRU_MSG (only actual data bytes are sent).
// ===========================================================================
struct WireMsg {
    uint32_t ProtocolID;
    uint32_t RxStatus;
    uint32_t TxFlags;
    uint32_t Timestamp;
    uint32_t DataSize;
    uint32_t ExtraDataIndex;
    // Followed by DataSize bytes (variable length on the wire).
    // For in-memory representation, use max buffer:
    uint8_t  Data[4128];

    // Size of the header portion (without trailing Data padding)
    static constexpr uint32_t header_size() { return 6 * sizeof(uint32_t); }
    // Wire size: header + actual data bytes
    uint32_t wire_size() const { return header_size() + DataSize; }
};

// ===========================================================================
// Per-function request/response payloads
// ===========================================================================

// --- PassThruOpen ---
struct OpenRequest {
    char name[256];          // device name (null-terminated)
};
struct OpenResponse {
    uint32_t device_id;
};

// --- PassThruClose ---
struct CloseRequest {
    uint32_t device_id;
};

// --- PassThruConnect ---
struct ConnectRequest {
    uint32_t device_id;
    uint32_t protocol_id;
    uint32_t flags;
    uint32_t baudrate;
};
struct ConnectResponse {
    uint32_t channel_id;
};

// --- PassThruDisconnect ---
struct DisconnectRequest {
    uint32_t channel_id;
};

// --- PassThruReadMsgs ---
struct ReadMsgsRequest {
    uint32_t channel_id;
    uint32_t max_msgs;
    uint32_t timeout;
};
// Response payload: uint32_t num_msgs, then num_msgs × WireMsg (variable)

// --- PassThruWriteMsgs ---
struct WriteMsgsRequest {
    uint32_t channel_id;
    uint32_t num_msgs;
    uint32_t timeout;
    // Followed by num_msgs × WireMsg (variable)
};
// Response payload: uint32_t num_written

// --- PassThruStartMsgFilter ---
struct StartFilterRequest {
    uint32_t channel_id;
    uint32_t filter_type;
    uint8_t  has_mask;
    uint8_t  has_pattern;
    uint8_t  has_flow_control;
    uint8_t  reserved;
    // Followed by 0–3 WireMsg (mask, pattern, flow_control — if present)
};
struct StartFilterResponse {
    uint32_t filter_id;
};

// --- PassThruStopMsgFilter ---
struct StopFilterRequest {
    uint32_t channel_id;
    uint32_t filter_id;
};

// --- PassThruStartPeriodicMsg ---
struct StartPeriodicRequest {
    uint32_t channel_id;
    uint32_t time_interval;
    // Followed by 1 × WireMsg
};
struct StartPeriodicResponse {
    uint32_t msg_id;
};

// --- PassThruStopPeriodicMsg ---
struct StopPeriodicRequest {
    uint32_t channel_id;
    uint32_t msg_id;
};

// --- PassThruIoctl ---
struct IoctlRequest {
    uint32_t channel_id;
    uint32_t ioctl_id;
    uint32_t input_size;     // bytes of input data following
    // Followed by input_size bytes
};
// Response payload: output data bytes (variable)

// --- PassThruReadVersion ---
struct ReadVersionRequest {
    uint32_t device_id;
};
struct ReadVersionResponse {
    char firmware[80];
    char dll_version[80];
    char api_version[80];
};

// --- PassThruGetLastError ---
// No request payload.
struct GetLastErrorResponse {
    char description[80];
};

#pragma pack(pop)

} // namespace proxy
} // namespace canmatik
