/// @file j2534_provider.cpp
/// J2534Provider : IDeviceProvider — real Windows adapter backend (T028 — US1).
/// Every J2534 API call is logged at debug level per Constitution Principle II.

#include "platform/win32/j2534_provider.h"
#include "platform/win32/j2534_channel.h"
#include "transport/transport_error.h"

#include "core/log_macros.h"

namespace canmatik {

J2534Provider::~J2534Provider() {
    if (connected_) {
        try { disconnect(); } catch (...) {}
    }
}

std::vector<DeviceInfo> J2534Provider::enumerate() {
    LOG_DEBUG("J2534Provider::enumerate() — scanning Windows Registry");
    auto providers = scanner_.scan();
    LOG_DEBUG("J2534Provider::enumerate() — found {} provider(s)", providers.size());
    return providers;
}

std::unique_ptr<IChannel> J2534Provider::connect(const DeviceInfo& dev) {
    LOG_DEBUG("J2534Provider::connect() — device: '{}', DLL: '{}'", dev.name, dev.dll_path);

    // If already connected to a different device, disconnect first
    if (connected_) {
        LOG_DEBUG("J2534Provider::connect() — disconnecting previous device");
        disconnect();
    }

    // Load the J2534 DLL
    dll_.load(dev.dll_path);

    // Call PassThruOpen
    LOG_DEBUG("Calling PassThruOpen(nullptr, &deviceID)");
    unsigned long device_id = 0;
    long ret = dll_.PassThruOpen(nullptr, &device_id);
    LOG_DEBUG("PassThruOpen returned {} ({})", ret, j2534::status_to_string(ret));

    if (ret != j2534::STATUS_NOERROR) {
        // Retrieve detailed error message
        char err_buf[256] = {};
        if (dll_.PassThruGetLastError) {
            dll_.PassThruGetLastError(err_buf);
        }
        std::string msg = std::string("PassThruOpen failed: ") + err_buf;
        LOG_ERROR("{}", msg);
        dll_.unload();
        throw TransportError(static_cast<int32_t>(ret), msg, "PassThruOpen");
    }

    device_id_ = device_id;
    connected_ = true;
    LOG_INFO("Connected to device '{}' (device ID: {})", dev.name, device_id_);

    // Read and log version info if available
    if (dll_.PassThruReadVersion) {
        auto ver = read_version();
        LOG_DEBUG("  Firmware: {}", ver.firmware);
        LOG_DEBUG("  DLL:      {}", ver.dll);
        LOG_DEBUG("  API:      {}", ver.api);
    }

    // Create and return a J2534Channel wrapping this connection
    return std::make_unique<J2534Channel>(dll_, device_id_);
}

void J2534Provider::disconnect() {
    if (!connected_) return;

    LOG_DEBUG("Calling PassThruClose({})", device_id_);
    long ret = dll_.PassThruClose(device_id_);
    LOG_DEBUG("PassThruClose returned {} ({})", ret, j2534::status_to_string(ret));

    if (ret != j2534::STATUS_NOERROR) {
        char err_buf[256] = {};
        if (dll_.PassThruGetLastError) {
            dll_.PassThruGetLastError(err_buf);
        }
        LOG_WARNING("PassThruClose failed: {} — {}", ret, err_buf);
    }

    connected_ = false;
    device_id_ = 0;
    dll_.unload();
    LOG_DEBUG("J2534Provider::disconnect() complete");
}

J2534Provider::VersionInfo J2534Provider::read_version() const {
    VersionInfo info;
    if (!connected_ || !dll_.PassThruReadVersion) return info;

    char fw[256] = {};
    char dll_ver[256] = {};
    char api[256] = {};

    LOG_DEBUG("Calling PassThruReadVersion({})", device_id_);
    long ret = dll_.PassThruReadVersion(device_id_, fw, dll_ver, api);
    LOG_DEBUG("PassThruReadVersion returned {} ({})", ret, j2534::status_to_string(ret));

    if (ret == j2534::STATUS_NOERROR) {
        info.firmware = fw;
        info.dll = dll_ver;
        info.api = api;
    }
    return info;
}

} // namespace canmatik
