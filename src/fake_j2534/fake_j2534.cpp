/// @file fake_j2534.cpp
/// Fake J2534 Pass-Thru DLL — appears as a real USB CAN adapter to any
/// J2534-compliant tool.  Every API call is logged to a file for sniffing
/// tool behavior.  Later wired to an ECU simulation back-end.
///
/// Exports the 13 mandatory J2534-1 functions (__stdcall).
/// Build: shared library (DLL), 32-bit, no CRT dependencies beyond msvcrt.

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
// Device / Channel state
// ============================================================================
constexpr unsigned long FAKE_DEVICE_ID  = 1;
constexpr unsigned long FAKE_CHANNEL_ID = 1;
constexpr unsigned long MAX_FILTERS     = 10;

struct FakeState {
    bool device_open   = false;
    bool channel_open  = false;
    unsigned long protocol     = 0;
    unsigned long baudrate     = 0;
    unsigned long flags        = 0;
    unsigned long next_filter  = 1;
    unsigned long filter_count = 0;
    unsigned long hw_timestamp = 0;      // µs, incremented on read
    char last_error[256]       = {};
};

std::mutex  g_state_mutex;
FakeState   g_state;

void set_last_error(const char* msg) {
    strncpy(g_state.last_error, msg, sizeof(g_state.last_error) - 1);
    g_state.last_error[sizeof(g_state.last_error) - 1] = '\0';
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

    if (g_state.device_open) {
        set_last_error("Device already open");
        j2534_log("  -> ERR_DEVICE_IN_USE");
        return J2534_ERR_DEVICE_IN_USE;
    }

    g_state = FakeState{};
    g_state.device_open = true;
    *pDeviceID = FAKE_DEVICE_ID;

    j2534_log("  -> OK, DeviceID=%lu", *pDeviceID);
    return J2534_OK;
}

EXPORT PassThruClose(unsigned long DeviceID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruClose(DeviceID=%lu)", DeviceID);

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

    if (DeviceID != FAKE_DEVICE_ID || !g_state.device_open) {
        set_last_error("Invalid device ID");
        return J2534_ERR_INVALID_DEVICE_ID;
    }
    if (!pChannelID) {
        set_last_error("pChannelID is NULL");
        return J2534_ERR_NULL_PARAMETER;
    }
    if (ProtocolID != PROTOCOL_CAN && ProtocolID != PROTOCOL_ISO15765) {
        set_last_error("Unsupported protocol");
        j2534_log("  -> ERR_INVALID_PROTOCOL");
        return J2534_ERR_INVALID_PROTOCOL;
    }
    if (g_state.channel_open) {
        set_last_error("Channel already open");
        j2534_log("  -> ERR_EXCEEDED_LIMIT");
        return J2534_ERR_EXCEEDED_LIMIT;
    }

    g_state.channel_open = true;
    g_state.protocol     = ProtocolID;
    g_state.baudrate     = Baudrate;
    g_state.flags        = Flags;
    g_state.next_filter  = 1;
    g_state.filter_count = 0;
    *pChannelID = FAKE_CHANNEL_ID;

    j2534_log("  -> OK, ChannelID=%lu", *pChannelID);
    return J2534_OK;
}

EXPORT PassThruDisconnect(unsigned long ChannelID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruDisconnect(ChannelID=%lu)", ChannelID);

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }

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

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }
    if (!pMsg || !pNumMsgs) {
        set_last_error("NULL parameter");
        return J2534_ERR_NULL_PARAMETER;
    }

    // For now: empty bus — return timeout after sleeping the requested duration.
    // ECU simulation will inject frames here in the future.
    if (Timeout > 0) {
        // Release lock during sleep so other threads aren't blocked
        g_state_mutex.unlock();
        Sleep(Timeout);
        g_state_mutex.lock();
    }

    *pNumMsgs = 0;
    j2534_log("  -> ERR_BUFFER_EMPTY (silent bus)");
    return J2534_ERR_BUFFER_EMPTY;
}

EXPORT PassThruWriteMsgs(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
                         unsigned long* pNumMsgs, unsigned long Timeout) {
    std::lock_guard<std::mutex> lk(g_state_mutex);

    unsigned long count = (pNumMsgs ? *pNumMsgs : 0);
    j2534_log("PassThruWriteMsgs(ChannelID=%lu, NumMsgs=%lu, Timeout=%lu)",
              ChannelID, count, Timeout);

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }

    // Log each message for sniffing
    for (unsigned long i = 0; i < count && pMsg; ++i) {
        char label[32];
        sprintf(label, "TX[%lu]", i);
        log_msg(label, &pMsg[i]);
    }

    // Accept all — future ECU sim will process these
    j2534_log("  -> OK (accepted %lu msgs)", count);
    return J2534_OK;
}

EXPORT PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                               const PASSTHRU_MSG* pMaskMsg,
                               const PASSTHRU_MSG* pPatternMsg,
                               const PASSTHRU_MSG* pFlowControlMsg,
                               unsigned long* pMsgID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStartMsgFilter(ChannelID=%lu, FilterType=%lu)", ChannelID, FilterType);

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }
    if (!pMsgID) {
        set_last_error("pMsgID is NULL");
        return J2534_ERR_NULL_PARAMETER;
    }
    if (g_state.filter_count >= MAX_FILTERS) {
        set_last_error("Filter limit reached");
        return J2534_ERR_EXCEEDED_LIMIT;
    }

    log_msg("Mask", pMaskMsg);
    log_msg("Pattern", pPatternMsg);
    log_msg("FlowControl", pFlowControlMsg);

    *pMsgID = g_state.next_filter++;
    g_state.filter_count++;

    j2534_log("  -> OK, FilterID=%lu (total=%lu)", *pMsgID, g_state.filter_count);
    return J2534_OK;
}

EXPORT PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStopMsgFilter(ChannelID=%lu, MsgID=%lu)", ChannelID, MsgID);

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }

    if (g_state.filter_count > 0) g_state.filter_count--;
    j2534_log("  -> OK (remaining filters=%lu)", g_state.filter_count);
    return J2534_OK;
}

EXPORT PassThruStartPeriodicMsg(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
                                unsigned long* pMsgID, unsigned long TimeInterval) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStartPeriodicMsg(ChannelID=%lu, Interval=%lu)", ChannelID, TimeInterval);
    log_msg("Periodic", pMsg);

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }
    if (pMsgID) *pMsgID = 1;

    j2534_log("  -> OK");
    return J2534_OK;
}

EXPORT PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruStopPeriodicMsg(ChannelID=%lu, MsgID=%lu)", ChannelID, MsgID);

    if (ChannelID != FAKE_CHANNEL_ID || !g_state.channel_open) {
        set_last_error("Invalid channel ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }

    j2534_log("  -> OK");
    return J2534_OK;
}

EXPORT PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                     void* pInput, void* pOutput) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruIoctl(ChannelID=%lu, Ioctl=%s(%lu))",
              ChannelID, ioctl_name(IoctlID), IoctlID);

    // READ_VBATT uses DeviceID, not ChannelID — accept any
    if (IoctlID == IOCTL_READ_VBATT) {
        if (pOutput) {
            *static_cast<unsigned long*>(pOutput) = 12100; // 12.1V
            j2534_log("  -> OK, Vbatt=12100 mV");
        }
        return J2534_OK;
    }

    // For channel-scoped IOCTLs, validate channel
    if (ChannelID != FAKE_CHANNEL_ID && ChannelID != FAKE_DEVICE_ID) {
        set_last_error("Invalid channel/device ID for IOCTL");
        j2534_log("  -> ERR_INVALID_CHANNEL_ID");
        return J2534_ERR_INVALID_CHANNEL_ID;
    }

    switch (IoctlID) {
        case IOCTL_GET_CONFIG: {
            auto* list = static_cast<SCONFIG_LIST*>(pInput);
            if (list && list->ConfigPtr) {
                for (unsigned long i = 0; i < list->NumOfParams; ++i) {
                    j2534_log("  GET_CONFIG: Param=0x%04lX", list->ConfigPtr[i].Parameter);
                    // Return data rate if requested
                    if (list->ConfigPtr[i].Parameter == 0x01)
                        list->ConfigPtr[i].Value = g_state.baudrate;
                    else
                        list->ConfigPtr[i].Value = 0;
                }
            }
            j2534_log("  -> OK");
            return J2534_OK;
        }
        case IOCTL_SET_CONFIG: {
            auto* list = static_cast<SCONFIG_LIST*>(pInput);
            if (list && list->ConfigPtr) {
                for (unsigned long i = 0; i < list->NumOfParams; ++i) {
                    j2534_log("  SET_CONFIG: Param=0x%04lX = %lu",
                              list->ConfigPtr[i].Parameter, list->ConfigPtr[i].Value);
                }
            }
            j2534_log("  -> OK");
            return J2534_OK;
        }
        case IOCTL_CLEAR_TX_BUFFER:
        case IOCTL_CLEAR_RX_BUFFER:
        case IOCTL_CLEAR_PERIODIC_MSGS:
        case IOCTL_CLEAR_MSG_FILTERS:
            g_state.filter_count = 0;
            j2534_log("  -> OK (cleared)");
            return J2534_OK;
        default:
            j2534_log("  -> ERR_INVALID_IOCTL (unsupported)");
            set_last_error("Unsupported IOCTL");
            return J2534_ERR_INVALID_IOCTL;
    }
}

EXPORT PassThruReadVersion(unsigned long DeviceID, char* pFirmwareVersion,
                           char* pDllVersion, char* pApiVersion) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    j2534_log("PassThruReadVersion(DeviceID=%lu)", DeviceID);

    if (DeviceID != FAKE_DEVICE_ID || !g_state.device_open) {
        set_last_error("Invalid device ID");
        return J2534_ERR_INVALID_DEVICE_ID;
    }

    if (pFirmwareVersion) strcpy(pFirmwareVersion, "1.00");
    if (pDllVersion)      strcpy(pDllVersion,      "1.00");
    if (pApiVersion)      strcpy(pApiVersion,       "04.04");

    j2534_log("  -> OK (FW=1.00 DLL=1.00 API=04.04)");
    return J2534_OK;
}

EXPORT PassThruGetLastError(char* pErrorDescription) {
    std::lock_guard<std::mutex> lk(g_state_mutex);

    if (pErrorDescription) {
        strncpy(pErrorDescription, g_state.last_error, 79);
        pErrorDescription[79] = '\0';
    }
    // Don't log this one to avoid feedback loop
    return J2534_OK;
}
