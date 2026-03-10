/// @file session_service.cpp
/// SessionService — connect, disconnect, channel management (T030 — US1).

#include "services/session_service.h"
#include "transport/transport_error.h"

#include "core/log_macros.h"
#include <stdexcept>

namespace canmatik {

SessionService::~SessionService() {
    try { disconnect(); } catch (...) {}
}

void SessionService::setProvider(std::unique_ptr<IDeviceProvider> provider) {
    if (channel_) {
        disconnect();
    }
    provider_ = std::move(provider);
    LOG_DEBUG("SessionService: provider set");
}

std::vector<DeviceInfo> SessionService::scan() {
    if (!provider_) {
        LOG_WARNING("SessionService::scan() called with no provider set");
        return {};
    }
    LOG_DEBUG("SessionService::scan() — delegating to provider");
    return provider_->enumerate();
}

void SessionService::connect(const DeviceInfo& dev) {
    if (!provider_) {
        throw TransportError(0, "No provider set — call setProvider() first",
                             "SessionService::connect");
    }

    // Disconnect any existing session
    if (channel_) {
        LOG_DEBUG("SessionService::connect() — disconnecting previous session");
        disconnect();
    }

    LOG_INFO("Connecting to device: '{}' (vendor: {})", dev.name, dev.vendor);
    channel_ = provider_->connect(dev);

    status_.provider_name = dev.name;
    status_.adapter_name = dev.name;
    LOG_INFO("Connected to '{}'", dev.name);
}

void SessionService::disconnect() {
    if (status_.channel_open) {
        closeChannel();
    }
    if (channel_) {
        LOG_DEBUG("SessionService::disconnect()");
        channel_.reset();
        status_.provider_name.clear();
        status_.adapter_name.clear();
    }
}

void SessionService::openChannel(uint32_t bitrate) {
    if (!channel_) {
        throw TransportError(0, "Not connected to a device — cannot open channel",
                             "SessionService::openChannel");
    }
    if (status_.channel_open) {
        LOG_DEBUG("SessionService::openChannel() — closing existing channel first");
        closeChannel();
    }

    LOG_INFO("Opening CAN channel at {} bps", bitrate);
    channel_->open(bitrate);

    status_.bitrate = bitrate;
    status_.channel_open = true;
    status_.session_start = std::chrono::steady_clock::now();
    status_.frames_received = 0;
    status_.frames_transmitted = 0;
    status_.errors = 0;
    status_.dropped = 0;
    LOG_INFO("CAN channel open at {} bps", bitrate);
}

void SessionService::closeChannel() {
    if (channel_ && status_.channel_open) {
        LOG_DEBUG("SessionService::closeChannel()");
        channel_->close();
        status_.channel_open = false;
        status_.bitrate = 0;
    }
}

IChannel* SessionService::channel() {
    return channel_.get();
}

const SessionStatus& SessionService::status() const {
    return status_;
}

SessionStatus& SessionService::mutableStatus() {
    return status_;
}

} // namespace canmatik
