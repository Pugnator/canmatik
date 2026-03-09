#pragma once

/// @file registry_scanner.h
/// Windows Registry scanner for J2534 provider discovery (T027 — US1).
/// Scans HKLM\SOFTWARE\PassThruSupport.04.04 with KEY_WOW64_32KEY.

#include <vector>
#include "transport/device_info.h"

#ifndef _WIN32
#error "registry_scanner.h is a Windows-only header"
#endif

namespace canmatik {

/// Scan the Windows Registry for installed J2534 Pass-Thru providers.
/// Each subkey under HKLM\SOFTWARE\PassThruSupport.04.04 represents one provider.
class RegistryScanner {
public:
    /// Scan the registry and return all discovered providers.
    /// @throws TransportError if the registry root key cannot be opened.
    [[nodiscard]] std::vector<DeviceInfo> scan();
};

} // namespace canmatik
