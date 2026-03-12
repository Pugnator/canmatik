#pragma once

/// @file j2534_defs.h
/// J2534 Pass-Thru API types, constants, function pointer typedefs with
/// __stdcall calling convention, and PASSTHRU_MSG struct (T025 — US1).
///
/// Based on SAE J2534-1 (2004) — the de-facto standard that virtually all
/// USB-attached OBD-II / CAN Pass-Thru adapters implement.

#include <cstdint>

#ifndef _WIN32
#error "j2534_defs.h is a Windows-only header"
#endif

namespace canmatik {
namespace j2534 {

// ===========================================================================
// Status codes
// ===========================================================================
constexpr long STATUS_NOERROR           = 0x00;
constexpr long ERR_NOT_SUPPORTED        = 0x01;
constexpr long ERR_INVALID_CHANNEL_ID   = 0x02;
constexpr long ERR_INVALID_PROTOCOL_ID  = 0x03;
constexpr long ERR_NULL_PARAMETER       = 0x04;
constexpr long ERR_INVALID_IOCTL_VALUE  = 0x05;
constexpr long ERR_INVALID_FLAGS        = 0x06;
constexpr long ERR_FAILED               = 0x07;
constexpr long ERR_DEVICE_NOT_CONNECTED = 0x08;
constexpr long ERR_TIMEOUT              = 0x09;
constexpr long ERR_INVALID_MSG          = 0x0A;
constexpr long ERR_INVALID_TIME_INTERVAL= 0x0B;
constexpr long ERR_EXCEEDED_LIMIT       = 0x0C;
constexpr long ERR_INVALID_MSG_ID       = 0x0D;
constexpr long ERR_DEVICE_IN_USE        = 0x0E;
constexpr long ERR_INVALID_IOCTL_ID     = 0x0F;
constexpr long ERR_BUFFER_EMPTY         = 0x10;
constexpr long ERR_BUFFER_FULL          = 0x11;
constexpr long ERR_BUFFER_OVERFLOW      = 0x12;
constexpr long ERR_PIN_INVALID          = 0x13;
constexpr long ERR_CHANNEL_IN_USE       = 0x14;
constexpr long ERR_MSG_PROTOCOL_ID      = 0x15;
constexpr long ERR_INVALID_FILTER_ID    = 0x16;
constexpr long ERR_NO_FLOW_CONTROL      = 0x17;
constexpr long ERR_NOT_UNIQUE           = 0x18;
constexpr long ERR_INVALID_BAUDRATE     = 0x19;
constexpr long ERR_INVALID_DEVICE_ID    = 0x1A;

// ===========================================================================
// Protocol IDs
// ===========================================================================
constexpr unsigned long PROTOCOL_J1850_VPW  = 0x01;
constexpr unsigned long PROTOCOL_J1850_PWM  = 0x02;
constexpr unsigned long PROTOCOL_CAN        = 0x05;
constexpr unsigned long PROTOCOL_ISO15765   = 0x06;
constexpr unsigned long PROTOCOL_CAN_PS     = 0x8004;  // CAN with Pin Select

// ===========================================================================
// IOCTL IDs
// ===========================================================================
constexpr unsigned long IOCTL_GET_CONFIG            = 0x01;
constexpr unsigned long IOCTL_SET_CONFIG            = 0x02;
constexpr unsigned long IOCTL_READ_VBATT            = 0x03;
constexpr unsigned long IOCTL_FIVE_BAUD_INIT        = 0x04;
constexpr unsigned long IOCTL_FAST_INIT             = 0x05;
constexpr unsigned long IOCTL_CLEAR_TX_BUFFER       = 0x07;
constexpr unsigned long IOCTL_CLEAR_RX_BUFFER       = 0x08;
constexpr unsigned long IOCTL_CLEAR_PERIODIC_MSGS   = 0x09;
constexpr unsigned long IOCTL_CLEAR_MSG_FILTERS     = 0x0A;
constexpr unsigned long IOCTL_CLEAR_FUNCT_MSG_LOOKUP_TABLE = 0x0B;
constexpr unsigned long IOCTL_ADD_TO_FUNCT_MSG_LOOKUP_TABLE = 0x0C;
constexpr unsigned long IOCTL_DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE = 0x0D;
constexpr unsigned long IOCTL_READ_PROG_VOLTAGE     = 0x0E;

// ===========================================================================
// Config Parameter IDs (for GET_CONFIG / SET_CONFIG)
// ===========================================================================
constexpr unsigned long PARAM_DATA_RATE     = 0x01;
constexpr unsigned long PARAM_LOOPBACK      = 0x03;
constexpr unsigned long PARAM_NODE_ADDRESS  = 0x04;
constexpr unsigned long PARAM_NETWORK_LINE  = 0x05;
constexpr unsigned long PARAM_P1_MIN        = 0x06;
constexpr unsigned long PARAM_P1_MAX        = 0x07;
constexpr unsigned long PARAM_P2_MIN        = 0x08;
constexpr unsigned long PARAM_P2_MAX        = 0x09;
constexpr unsigned long PARAM_P3_MIN        = 0x0A;
constexpr unsigned long PARAM_P3_MAX        = 0x0B;
constexpr unsigned long PARAM_P4_MIN        = 0x0C;
constexpr unsigned long PARAM_P4_MAX        = 0x0D;

// ===========================================================================
// Connect flags
// ===========================================================================
constexpr unsigned long CAN_29BIT_ID        = 0x00000100;
constexpr unsigned long ISO9141_NO_CHECKSUM = 0x00000200;
constexpr unsigned long CAN_ID_BOTH         = 0x00000800;
constexpr unsigned long ISO9141_K_LINE_ONLY = 0x00001000;

// ===========================================================================
// Receive status bits in PASSTHRU_MSG.RxStatus
// ===========================================================================
constexpr unsigned long RX_MSG_CAN_29BIT_ID       = 0x00000100;
constexpr unsigned long RX_MSG_ISO15765_FIRST_FRAME = 0x00000200;
constexpr unsigned long RX_MSG_ISO15765_EXT_ADDR  = 0x00000200;
constexpr unsigned long RX_MSG_TX_DONE            = 0x00000008;
constexpr unsigned long RX_MSG_START_OF_MESSAGE   = 0x00000002;

// ===========================================================================
// Filter types for PassThruStartMsgFilter
// ===========================================================================
constexpr unsigned long FILTER_PASS   = 0x01;
constexpr unsigned long FILTER_BLOCK  = 0x02;
constexpr unsigned long FILTER_FLOW_CONTROL = 0x03;

// ===========================================================================
// PASSTHRU_MSG — the J2534 message structure
// ===========================================================================
constexpr size_t PASSTHRU_MSG_DATA_SIZE = 4128;

#pragma pack(push, 1)
struct PASSTHRU_MSG {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;         ///< µs — adapter HW clock (32-bit, wraps)
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[PASSTHRU_MSG_DATA_SIZE];
};
#pragma pack(pop)

// ===========================================================================
// SCONFIG_LIST / SCONFIG — for IOCTL GET/SET CONFIG
// ===========================================================================
struct SCONFIG {
    unsigned long Parameter;
    unsigned long Value;
};

struct SCONFIG_LIST {
    unsigned long NumOfParams;
    SCONFIG* ConfigPtr;
};

// ===========================================================================
// Function pointer typedefs — __stdcall calling convention
// ===========================================================================
using PassThruOpen_t           = long (__stdcall*)(void* pName, unsigned long* pDeviceID);
using PassThruClose_t          = long (__stdcall*)(unsigned long DeviceID);
using PassThruConnect_t        = long (__stdcall*)(unsigned long DeviceID, unsigned long ProtocolID,
                                                    unsigned long Flags, unsigned long Baudrate,
                                                    unsigned long* pChannelID);
using PassThruDisconnect_t     = long (__stdcall*)(unsigned long ChannelID);
using PassThruReadMsgs_t       = long (__stdcall*)(unsigned long ChannelID, PASSTHRU_MSG* pMsg,
                                                    unsigned long* pNumMsgs, unsigned long Timeout);
using PassThruWriteMsgs_t      = long (__stdcall*)(unsigned long ChannelID, const PASSTHRU_MSG* pMsg,
                                                    unsigned long* pNumMsgs, unsigned long Timeout);
using PassThruStartMsgFilter_t = long (__stdcall*)(unsigned long ChannelID, unsigned long FilterType,
                                                    const PASSTHRU_MSG* pMaskMsg,
                                                    const PASSTHRU_MSG* pPatternMsg,
                                                    const PASSTHRU_MSG* pFlowControlMsg,
                                                    unsigned long* pMsgID);
using PassThruStopMsgFilter_t  = long (__stdcall*)(unsigned long ChannelID, unsigned long MsgID);
using PassThruStartPeriodicMsg_t = long (__stdcall*)(unsigned long ChannelID,
                                                      const PASSTHRU_MSG* pMsg,
                                                      unsigned long* pMsgID,
                                                      unsigned long TimeInterval);
using PassThruStopPeriodicMsg_t = long (__stdcall*)(unsigned long ChannelID, unsigned long MsgID);
using PassThruIoctl_t          = long (__stdcall*)(unsigned long ChannelID, unsigned long IoctlID,
                                                    void* pInput, void* pOutput);
using PassThruReadVersion_t    = long (__stdcall*)(unsigned long DeviceID, char* pFirmwareVersion,
                                                    char* pDllVersion, char* pApiVersion);
using PassThruGetLastError_t   = long (__stdcall*)(char* pErrorDescription);

// ===========================================================================
// Helper: human-readable error name
// ===========================================================================
/// Map a J2534 status code to a short descriptive string.
[[nodiscard]] inline const char* status_to_string(long code) {
    switch (code) {
        case STATUS_NOERROR:           return "STATUS_NOERROR";
        case ERR_NOT_SUPPORTED:        return "ERR_NOT_SUPPORTED";
        case ERR_INVALID_CHANNEL_ID:   return "ERR_INVALID_CHANNEL_ID";
        case ERR_INVALID_PROTOCOL_ID:  return "ERR_INVALID_PROTOCOL_ID";
        case ERR_NULL_PARAMETER:       return "ERR_NULL_PARAMETER";
        case ERR_INVALID_IOCTL_VALUE:  return "ERR_INVALID_IOCTL_VALUE";
        case ERR_INVALID_FLAGS:        return "ERR_INVALID_FLAGS";
        case ERR_FAILED:               return "ERR_FAILED";
        case ERR_DEVICE_NOT_CONNECTED: return "ERR_DEVICE_NOT_CONNECTED";
        case ERR_TIMEOUT:              return "ERR_TIMEOUT";
        case ERR_INVALID_MSG:          return "ERR_INVALID_MSG";
        case ERR_EXCEEDED_LIMIT:       return "ERR_EXCEEDED_LIMIT";
        case ERR_DEVICE_IN_USE:        return "ERR_DEVICE_IN_USE";
        case ERR_BUFFER_EMPTY:         return "ERR_BUFFER_EMPTY";
        case ERR_BUFFER_FULL:          return "ERR_BUFFER_FULL";
        case ERR_BUFFER_OVERFLOW:      return "ERR_BUFFER_OVERFLOW";
        case ERR_INVALID_BAUDRATE:     return "ERR_INVALID_BAUDRATE";
        case ERR_INVALID_DEVICE_ID:    return "ERR_INVALID_DEVICE_ID";
        default:                       return "UNKNOWN_J2534_ERROR";
    }
}

} // namespace j2534
} // namespace canmatik
