/// @file proxy_registry.cpp
/// Install / uninstall fake J2534 DLL in Windows registry.

#include "proxy/proxy_registry.h"
#include "core/log_macros.h"

#include <filesystem>
#include <format>

namespace canmatik {

static constexpr const char* kRegistryRoot = "SOFTWARE\\PassThruSupport.04.04";
static constexpr const char* kDeviceClassesRoot =
    "System\\CurrentControlSet\\Control\\DeviceClasses\\"
    "{6d1781b7-c987-4f6c-8d4f-1efc098bea67}";

// Build a sanitised device ID from a display name  (e.g. "OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" → "OPENPORT_2_0_J2534_ISO_CAN_VPW_PWM")
static std::string sanitize_device_id(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c >= 'A' && c <= 'Z')      out += c;
        else if (c >= 'a' && c <= 'z') out += static_cast<char>(c - 32);
        else if (c >= '0' && c <= '9') out += c;
        else if (c == ' ' || c == '-' || c == '.') out += '_';
    }
    return out;
}

// Build the DeviceClasses subkey name for a given device ID.
static std::string device_class_subkey(const std::string& device_id) {
    return std::format("##?#ROOT#{}#0000#{}", device_id, kJ2534DeviceGuid);
}

// Get the SysWOW64 directory path.
static std::string get_syswow64_dir() {
    char buf[MAX_PATH] = {};
    UINT len = GetSystemWow64DirectoryA(buf, MAX_PATH);
    if (len == 0) {
        // Fallback for 32-bit-only OS
        GetSystemDirectoryA(buf, MAX_PATH);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Enumerate
// ---------------------------------------------------------------------------
std::vector<J2534RegEntry> enumerate_j2534_providers() {
    std::vector<J2534RegEntry> result;

    HKEY root = nullptr;
    LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, kRegistryRoot, 0,
                             KEY_READ | KEY_WOW64_32KEY, &root);
    if (ret != ERROR_SUCCESS)
        return result;

    char subkey_name[256];
    for (DWORD i = 0; ; ++i) {
        DWORD name_size = sizeof(subkey_name);
        ret = RegEnumKeyExA(root, i, subkey_name, &name_size,
                            nullptr, nullptr, nullptr, nullptr);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret != ERROR_SUCCESS) continue;

        HKEY sub = nullptr;
        ret = RegOpenKeyExA(root, subkey_name, 0,
                            KEY_READ | KEY_WOW64_32KEY, &sub);
        if (ret != ERROR_SUCCESS) continue;

        auto read_str = [&](const char* val) -> std::string {
            char buf[512] = {};
            DWORD sz = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExA(sub, val, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS
                && type == REG_SZ) {
                if (sz > 0 && buf[sz - 1] == '\0') --sz;
                return {buf, sz};
            }
            return {};
        };

        J2534RegEntry entry;
        entry.subkey   = subkey_name;
        entry.name     = read_str("Name");
        entry.vendor   = read_str("Vendor");
        entry.dll_path = read_str("FunctionLibrary");
        if (entry.name.empty()) entry.name = subkey_name;

        RegCloseKey(sub);
        result.push_back(std::move(entry));
    }

    RegCloseKey(root);
    return result;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
std::string install_proxy_j2534(const std::string& display_name,
                                const std::string& vendor,
                                const std::string& dll_path,
                                const J2534Preset& preset) {
    if (display_name.empty()) return "Display name is empty";
    if (dll_path.empty())     return "DLL path is empty";

    // If the preset specifies an expected DLL name, deploy to SysWOW64 first
    std::string effective_dll = dll_path;
    if (preset.dll_name) {
        effective_dll = deploy_proxy_dll(dll_path, preset);
        if (effective_dll.empty())
            return std::format("Failed to deploy DLL as {} in SysWOW64. Run as Administrator.",
                               preset.dll_name);
    }

    std::string key_path = std::string(kRegistryRoot) + "\\" + display_name;

    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG ret = RegCreateKeyExA(HKEY_LOCAL_MACHINE, key_path.c_str(), 0, nullptr,
                               REG_OPTION_NON_VOLATILE,
                               KEY_WRITE | KEY_WOW64_32KEY,
                               nullptr, &key, &disposition);
    if (ret != ERROR_SUCCESS)
        return std::format("Cannot create registry key (error {}). Run as Administrator.", ret);

    auto set_str = [&](const char* name, const std::string& val) {
        RegSetValueExA(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(val.c_str()),
                       static_cast<DWORD>(val.size() + 1));
    };
    auto set_dword = [&](const char* name, DWORD val) {
        RegSetValueExA(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&val), sizeof(val));
    };

    set_str("Name",             display_name);
    set_str("Vendor",           vendor);
    set_str("FunctionLibrary",  effective_dll);
    set_str("ConfigApplication", "");
    set_dword("CAN",       preset.can       ? 1 : 0);
    set_dword("ISO15765",  preset.iso15765  ? 1 : 0);
    set_dword("J1850VPW",  preset.j1850_vpw ? 1 : 0);
    set_dword("J1850PWM",  preset.j1850_pwm ? 1 : 0);
    set_dword("ISO9141",   preset.iso9141   ? 1 : 0);
    set_dword("ISO14230",  preset.iso14230  ? 1 : 0);
    set_dword("SCI_A_ENGINE", preset.sci_a  ? 1 : 0);
    set_dword("SCI_B_ENGINE", preset.sci_b  ? 1 : 0);

    RegCloseKey(key);

    LOG_INFO("Installed proxy J2534 '{}' -> {}", display_name, effective_dll);

    // Also register under DeviceClasses so SetupDi-based tools find us
    auto dc_err = install_device_class_entry(display_name, effective_dll);
    if (!dc_err.empty())
        LOG_WARNING("DeviceClasses registration skipped: {}", dc_err);

    return {};
}

// ---------------------------------------------------------------------------
// Uninstall
// ---------------------------------------------------------------------------
std::string uninstall_j2534_provider(const std::string& subkey) {
    if (subkey.empty()) return "Subkey name is empty";

    // Clean up DeviceClasses entry
    uninstall_device_class_entry(subkey);

    // If this matches a known preset with a deployed DLL, remove it from SysWOW64
    for (int i = 0; i < kPresetCount; ++i) {
        if (subkey == kPresets[i].name) {
            remove_deployed_dll(kPresets[i]);
            break;
        }
    }

    std::string key_path = std::string(kRegistryRoot) + "\\" + subkey;
    LONG ret = RegDeleteKeyExA(HKEY_LOCAL_MACHINE, key_path.c_str(),
                               KEY_WOW64_32KEY, 0);
    if (ret != ERROR_SUCCESS)
        return std::format("Cannot delete registry key '{}' (error {}). Run as Administrator.",
                           subkey, ret);

    LOG_INFO("Uninstalled J2534 provider '{}'", subkey);
    return {};
}

// ---------------------------------------------------------------------------
// DeviceClasses registration — so SetupDi discovery also finds the proxy
// ---------------------------------------------------------------------------
std::string install_device_class_entry(const std::string& display_name,
                                       const std::string& dll_path) {
    if (display_name.empty()) return "Display name is empty";

    std::string dev_id = sanitize_device_id(display_name);
    std::string subkey = device_class_subkey(dev_id);

    // Open (or create) the GUID root key
    std::string root_path = kDeviceClassesRoot;
    HKEY root = nullptr;
    DWORD disp = 0;
    LONG ret = RegCreateKeyExA(HKEY_LOCAL_MACHINE, root_path.c_str(), 0, nullptr,
                               REG_OPTION_NON_VOLATILE,
                               KEY_WRITE | KEY_READ, nullptr, &root, &disp);
    if (ret != ERROR_SUCCESS)
        return std::format("Cannot open DeviceClasses key (error {}). Run as Administrator.", ret);

    // Create "Properties" subkey on the GUID root (some tools query this)
    HKEY props_key = nullptr;
    ret = RegCreateKeyExA(root, "Properties", 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &props_key, &disp);
    if (ret == ERROR_SUCCESS)
        RegCloseKey(props_key);

    // Create the device subkey  (##?#ROOT#...#0000#{guid})
    HKEY dev_key = nullptr;
    ret = RegCreateKeyExA(root, subkey.c_str(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &dev_key, &disp);
    if (ret != ERROR_SUCCESS) {
        RegCloseKey(root);
        return std::format("Cannot create device subkey (error {})", ret);
    }

    // DeviceInstance value on the device subkey itself
    std::string dev_instance = std::format("ROOT\\{}\\0000", dev_id);
    RegSetValueExA(dev_key, "DeviceInstance", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(dev_instance.c_str()),
                   static_cast<DWORD>(dev_instance.size() + 1));

    // Create "#" reference subkey  with SymbolicLink
    HKEY ref_key = nullptr;
    ret = RegCreateKeyExA(dev_key, "#", 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &ref_key, &disp);
    if (ret == ERROR_SUCCESS) {
        std::string symlink = std::format("\\\\?\\ROOT#{}#0000#{}", dev_id, kJ2534DeviceGuid);
        RegSetValueExA(ref_key, "SymbolicLink", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(symlink.c_str()),
                       static_cast<DWORD>(symlink.size() + 1));
        RegCloseKey(ref_key);
    }

    // Create "#\Device Parameters" with FunctionLibrary
    HKEY params_key = nullptr;
    ret = RegCreateKeyExA(dev_key, "#\\Device Parameters", 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &params_key, &disp);
    if (ret == ERROR_SUCCESS) {
        RegSetValueExA(params_key, "FunctionLibrary", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(dll_path.c_str()),
                       static_cast<DWORD>(dll_path.size() + 1));
        RegCloseKey(params_key);
    }

    // Create "Control" subkey with Linked = 1  (marks the interface as active)
    HKEY ctrl_key = nullptr;
    ret = RegCreateKeyExA(dev_key, "Control", 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &ctrl_key, &disp);
    if (ret == ERROR_SUCCESS) {
        DWORD linked = 1;
        RegSetValueExA(ctrl_key, "Linked", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&linked), sizeof(linked));
        RegCloseKey(ctrl_key);
    }

    RegCloseKey(dev_key);
    RegCloseKey(root);

    LOG_INFO("Registered DeviceClasses entry for '{}'", display_name);
    return {};
}

std::string uninstall_device_class_entry(const std::string& display_name) {
    if (display_name.empty()) return "Display name is empty";

    std::string dev_id = sanitize_device_id(display_name);
    std::string subkey = device_class_subkey(dev_id);
    std::string full_path = std::string(kDeviceClassesRoot) + "\\" + subkey;

    // Delete subtree: Control, #\Device Parameters, #, then the device key itself
    auto del = [](const std::string& path) {
        RegDeleteKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, 0);
    };
    del(full_path + "\\#\\Device Parameters");
    del(full_path + "\\#");
    del(full_path + "\\Control");
    del(full_path);

    LOG_INFO("Removed DeviceClasses entry for '{}'", display_name);
    return {};
}

// ---------------------------------------------------------------------------
// Deploy / remove DLL copy in SysWOW64
// ---------------------------------------------------------------------------
std::string deploy_proxy_dll(const std::string& src_dll,
                             const J2534Preset& preset) {
    if (!preset.dll_name || !preset.dll_name[0])
        return src_dll;  // no specific DLL name needed

    namespace fs = std::filesystem;
    std::string sys_dir = get_syswow64_dir();
    if (sys_dir.empty()) return {};

    fs::path dest = fs::path(sys_dir) / preset.dll_name;
    std::error_code ec;
    fs::copy_file(src_dll, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_WARNING("Failed to copy DLL to {}: {}", dest.string(), ec.message());
        return {};
    }
    LOG_INFO("Deployed {} -> {}", src_dll, dest.string());
    return dest.string();
}

void remove_deployed_dll(const J2534Preset& preset) {
    if (!preset.dll_name || !preset.dll_name[0])
        return;

    namespace fs = std::filesystem;
    std::string sys_dir = get_syswow64_dir();
    if (sys_dir.empty()) return;

    fs::path target = fs::path(sys_dir) / preset.dll_name;
    std::error_code ec;
    if (fs::remove(target, ec))
        LOG_INFO("Removed deployed DLL {}", target.string());
}

// ---------------------------------------------------------------------------
// Locate DLL next to exe
// ---------------------------------------------------------------------------
std::string find_proxy_dll_path() {
    char exe_path[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe_path, MAX_PATH))
        return {};
    std::filesystem::path p(exe_path);
    auto dll = p.parent_path() / "fake_j2534.dll";
    if (std::filesystem::exists(dll))
        return dll.string();
    return {};
}

} // namespace canmatik
