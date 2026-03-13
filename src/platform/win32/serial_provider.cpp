#include "platform/win32/serial_provider.h"
#include "platform/win32/serial_channel.h"
#include "core/log_macros.h"

#include <windows.h>
#include <vector>

namespace canmatik {

std::vector<DeviceInfo> SerialProvider::enumerate() {
    std::vector<DeviceInfo> list;

    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return list;
    }

    char value_name[256];
    BYTE data[256];
    DWORD value_index = 0;
    while (true) {
        DWORD name_len = sizeof(value_name);
        DWORD data_len = sizeof(data);
        DWORD type = 0;
        LONG ret = RegEnumValueA(key, value_index++, value_name, &name_len,
                                 nullptr, &type, data, &data_len);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret != ERROR_SUCCESS) continue;

        if (type != REG_SZ) continue;
        std::string port(reinterpret_cast<char*>(data), data_len - 1);
        DeviceInfo di;
        di.name = port + " (Serial)";
        di.vendor = "Serial";
        di.dll_path = "";
        di.supports_can = false;
        list.push_back(std::move(di));
    }

    RegCloseKey(key);
    return list;
}

std::unique_ptr<IChannel> SerialProvider::connect(const DeviceInfo& dev) {
    // Expect device name like "COM5 (Serial)" — extract COM port token
    auto pos = dev.name.find(" (");
    std::string port = (pos == std::string::npos) ? dev.name : dev.name.substr(0, pos);
    LOG_INFO("Connecting to serial port: {}", port);
    auto ch = std::make_unique<SerialChannel>(port);
    return ch;
}

} // namespace canmatik
