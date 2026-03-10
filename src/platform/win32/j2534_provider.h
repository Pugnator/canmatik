#pragma once

/// @file j2534_provider.h
/// J2534Provider : IDeviceProvider — real Windows adapter backend (T028 — US1).
/// Every J2534 API call is wrapped with LOG_DEBUG per Constitution Principle II.

#include "transport/device_provider.h"
#include "platform/win32/j2534_dll_loader.h"
#include "platform/win32/registry_scanner.h"

#include <memory>
#include <string>

namespace canmatik {

/// J2534 provider that discovers adapters via Windows Registry
/// and connects through the J2534 DLL interface.
class J2534Provider : public IDeviceProvider {
public:
    J2534Provider() = default;
    ~J2534Provider() override;

    /// Discover all J2534 providers installed on the system.
    std::vector<DeviceInfo> enumerate() override;

    /// Connect to the specified device (loads DLL, calls PassThruOpen).
    /// @throws TransportError on failure.
    std::unique_ptr<IChannel> connect(const DeviceInfo& dev) override;

    /// Disconnect current device (PassThruClose) and unload DLL.
    void disconnect();

    /// Get the device ID returned by PassThruOpen (valid while connected).
    [[nodiscard]] unsigned long device_id() const { return device_id_; }

    /// Check if currently connected to a device.
    [[nodiscard]] bool is_connected() const { return connected_; }

    /// Get a pointer to the loaded DLL (for channel creation).
    [[nodiscard]] J2534DllLoader* dll() { return &dll_; }

    /// Read firmware, DLL, and API versions (requires connection).
    struct VersionInfo {
        std::string firmware;
        std::string dll;
        std::string api;
    };
    [[nodiscard]] VersionInfo read_version() const;

private:
    RegistryScanner scanner_;
    J2534DllLoader dll_;
    unsigned long device_id_ = 0;
    bool connected_ = false;
};

} // namespace canmatik
