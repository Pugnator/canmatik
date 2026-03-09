#pragma once

/// @file device_info.h
/// Describes a discovered J2534 provider/adapter (USB-connected only).

#include <string>

namespace canmatik {

/// Read-only descriptor for a discovered J2534 provider.
/// Source: Windows Registry under HKLM\SOFTWARE\PassThruSupport.04.04\*
struct DeviceInfo {
    std::string name;             ///< Human-readable provider name
    std::string vendor;           ///< Vendor name
    std::string dll_path;         ///< Full path to J2534 DLL on host
    bool supports_can      = false; ///< Provider advertises CAN protocol support
    bool supports_iso15765 = false; ///< Provider advertises ISO 15765 support
};

} // namespace canmatik
