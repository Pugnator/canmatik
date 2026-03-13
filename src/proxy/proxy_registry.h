#pragma once

/// @file proxy_registry.h
/// Install / uninstall the fake J2534 DLL in the Windows registry,
/// and enumerate / remove existing J2534 providers.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace canmatik {

/// Describes a well-known J2534 interface preset that the proxy can mimic.
struct J2534Preset {
    const char* name;         ///< Registry subkey name + "Name" value
    const char* vendor;       ///< "Vendor" value
    const char* dll_name;     ///< Expected DLL filename (e.g. "op20pt32.dll"), nullptr = use source path
    bool can        = true;
    bool iso15765   = true;
    bool j1850_vpw  = false;
    bool j1850_pwm  = false;
    bool iso9141    = false;
    bool iso14230   = false;
    bool sci_a      = false;
    bool sci_b      = false;
};

/// Built-in presets for well-known interfaces.
inline const J2534Preset kPresets[] = {
    {"CANmatik Proxy",    "CANmatik",           nullptr,         true, true, false, false, false, false, false, false},
    {"Openport 2.0",      "Tactrix",            "op20pt32.dll",  true, true, true,  true,  true,  true,  true,  true },
    {"Scanmatic 2 Pro",   "Interface Solutions", nullptr,         true, true, true,  true,  true,  true,  false, false},
    {"MongoosePro GM II", "Drew Technologies",  nullptr,         true, true, true,  true,  true,  true,  false, false},
    {"VCM II",            "Ford / Bosch",       nullptr,         true, true, true,  true,  true,  true,  false, false},
};
inline constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

/// Minimal info for an installed J2534 provider (read from registry).
struct J2534RegEntry {
    std::string subkey;       ///< Registry subkey name under PassThruSupport.04.04
    std::string name;
    std::string vendor;
    std::string dll_path;
};

/// Enumerate all installed J2534 providers from the registry.
std::vector<J2534RegEntry> enumerate_j2534_providers();

/// Install (register) the fake J2534 DLL under a given name.
/// @param display_name  The "Name" value and registry subkey name.
/// @param vendor        The "Vendor" value.
/// @param dll_path      Absolute path to fake_j2534.dll.
/// @param preset        Protocol flags (CAN, ISO15765, etc.).
/// @return Empty on success, error description on failure.
std::string install_proxy_j2534(const std::string& display_name,
                                const std::string& vendor,
                                const std::string& dll_path,
                                const J2534Preset& preset);

/// Uninstall (remove) a J2534 provider registry key.
/// @param subkey  The subkey name under PassThruSupport.04.04.
/// @return Empty on success, error description on failure.
std::string uninstall_j2534_provider(const std::string& subkey);

/// Find the absolute path to fake_j2534.dll next to the running exe.
std::string find_proxy_dll_path();

/// J2534 Device Interface class GUID.
/// Some tools (e.g. romdrop) discover adapters via
/// HKLM\System\CurrentControlSet\Control\DeviceClasses\{this-guid}
/// instead of (or in addition to) PassThruSupport.04.04.
inline constexpr const char* kJ2534DeviceGuid =
    "{6d1781b7-c987-4f6c-8d4f-1efc098bea67}";

/// Register the proxy under the DeviceClasses interface GUID.
/// @return Empty on success, error description on failure.
std::string install_device_class_entry(const std::string& display_name,
                                       const std::string& dll_path);

/// Remove the DeviceClasses interface entry.
/// @return Empty on success, error description on failure.
std::string uninstall_device_class_entry(const std::string& display_name);

/// Deploy fake_j2534.dll to SysWOW64 under the preset's expected filename.
/// @param src_dll  Path to local fake_j2534.dll.
/// @param preset   Preset with dll_name (if nullptr, no copy is needed).
/// @return The effective DLL path to register (SysWOW64 copy or original).
std::string deploy_proxy_dll(const std::string& src_dll,
                             const J2534Preset& preset);

/// Remove the deployed DLL copy from SysWOW64.
void remove_deployed_dll(const J2534Preset& preset);

} // namespace canmatik
