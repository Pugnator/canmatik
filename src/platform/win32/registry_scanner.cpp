/// @file registry_scanner.cpp
/// Windows Registry scanner for J2534 providers (T027 — US1).

#include "platform/win32/registry_scanner.h"
#include "transport/transport_error.h"

#include "core/log_macros.h"
#include <windows.h>

#include <string>
#include <vector>

namespace canmatik {

// The root registry path for J2534 04.04 providers
static constexpr const char* kRegistryRoot = "SOFTWARE\\PassThruSupport.04.04";

/// Read a string value from an open registry key. Returns empty string if not found.
static std::string read_reg_string(HKEY key, const char* value_name) {
    char buffer[512] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LONG ret = ::RegQueryValueExA(key, value_name, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buffer), &size);
    if (ret == ERROR_SUCCESS && type == REG_SZ) {
        // Remove trailing null if present
        if (size > 0 && buffer[size - 1] == '\0') --size;
        return std::string(buffer, size);
    }
    return {};
}

/// Read a DWORD value from an open registry key. Returns 0 if not found.
static DWORD read_reg_dword(HKEY key, const char* value_name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG ret = ::RegQueryValueExA(key, value_name, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&value), &size);
    if (ret == ERROR_SUCCESS && type == REG_DWORD) {
        return value;
    }
    return 0;
}

std::vector<DeviceInfo> RegistryScanner::scan() {
    std::vector<DeviceInfo> providers;

    // Open the root key with 32-bit view (J2534 DLLs are typically 32-bit)
    HKEY root_key = nullptr;
    LONG ret = ::RegOpenKeyExA(HKEY_LOCAL_MACHINE, kRegistryRoot, 0,
                                KEY_READ | KEY_WOW64_32KEY, &root_key);
    if (ret != ERROR_SUCCESS) {
        LOG_DEBUG("J2534 registry root not found: HKLM\\{} (error {})",
                  kRegistryRoot, ret);
        // Not an error — just means no providers installed
        return providers;
    }

    LOG_DEBUG("Opened J2534 registry root: HKLM\\{}", kRegistryRoot);

    // Enumerate subkeys (each is a provider)
    char subkey_name[256] = {};
    for (DWORD index = 0; ; ++index) {
        DWORD name_size = sizeof(subkey_name);
        ret = ::RegEnumKeyExA(root_key, index, subkey_name, &name_size,
                               nullptr, nullptr, nullptr, nullptr);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret != ERROR_SUCCESS) {
            LOG_WARNING("Failed to enumerate J2534 registry subkey index {} (error {})",
                       index, ret);
            continue;
        }

        LOG_DEBUG("Found J2534 provider key: {}", subkey_name);

        // Open the provider subkey
        HKEY provider_key = nullptr;
        ret = ::RegOpenKeyExA(root_key, subkey_name, 0,
                               KEY_READ | KEY_WOW64_32KEY, &provider_key);
        if (ret != ERROR_SUCCESS) {
            LOG_WARNING("Cannot open provider key '{}' (error {})", subkey_name, ret);
            continue;
        }

        DeviceInfo info;
        info.name = read_reg_string(provider_key, "Name");
        info.vendor = read_reg_string(provider_key, "Vendor");
        info.dll_path = read_reg_string(provider_key, "FunctionLibrary");

        // Read protocol support flags (DWORD: CAN, ISO15765)
        info.supports_can      = (read_reg_dword(provider_key, "CAN") != 0);
        info.supports_iso15765 = (read_reg_dword(provider_key, "ISO15765") != 0);
        info.supports_j1850_vpw = (read_reg_dword(provider_key, "J1850VPW") != 0);
        info.supports_j1850_pwm = (read_reg_dword(provider_key, "J1850PWM") != 0);

        ::RegCloseKey(provider_key);

        // Use subkey name as fallback for missing name
        if (info.name.empty()) {
            info.name = subkey_name;
        }

        // Skip entries without a DLL path
        if (info.dll_path.empty()) {
            LOG_WARNING("Provider '{}' has no FunctionLibrary — skipping", info.name);
            continue;
        }

        LOG_DEBUG("  Name: {}", info.name);
        LOG_DEBUG("  Vendor: {}", info.vendor);
        LOG_DEBUG("  DLL: {}", info.dll_path);
        LOG_DEBUG("  CAN: {}, ISO15765: {}, J1850VPW: {}, J1850PWM: {}",
                  info.supports_can, info.supports_iso15765,
                  info.supports_j1850_vpw, info.supports_j1850_pwm);

        providers.push_back(std::move(info));
    }

    ::RegCloseKey(root_key);

    LOG_INFO("Registry scan complete: {} J2534 provider(s) found", providers.size());
    return providers;
}

} // namespace canmatik
