#pragma once

/// @file obd_session.h
/// OBD-II session — send queries, receive and decode responses via IChannel.

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"

#include "obd/dtc_decoder.h"
#include "obd/iso15765.h"
#include "obd/obd_request.h"
#include "obd/obd_response.h"
#include "obd/pid_decoder.h"
#include "obd/pid_table.h"
#include "transport/channel.h"

namespace canmatik {

/// Vehicle information from Mode $09.
struct VehicleInfo {
    std::string vin;
    std::string calibration_id;
    std::string ecu_name;
};

/// Result of a supported PIDs query.
struct SupportedPids {
    uint32_t ecu_id = 0;
    std::vector<uint8_t> pids;
};

class ObdSession {
public:
    /// @param channel Open CAN channel for communication.
    /// @param tx_id Request CAN ID (default: 0x7DF functional broadcast).
    /// @param rx_base Expected response base ID (default: 0x7E8).
    explicit ObdSession(IChannel& channel,
                        uint32_t tx_id = iso15765::kFunctionalTxId,
                        uint32_t rx_base = iso15765::kResponseBase);

    /// Query which PIDs are supported (Mode $01, PIDs $00/$20/$40).
    [[nodiscard]] Result<std::vector<SupportedPids>> query_supported_pids();

    /// Query a single PID and decode the value.
    [[nodiscard]] Result<DecodedPid> query_pid(uint8_t mode, uint8_t pid);

    /// Read stored DTCs (Mode $03).
    [[nodiscard]] Result<std::vector<DtcCode>> read_dtcs();

    /// Read pending DTCs (Mode $07).
    [[nodiscard]] Result<std::vector<DtcCode>> read_pending_dtcs();

    /// Clear DTCs (Mode $04). Requires force=true for safety.
    [[nodiscard]] Result<void> clear_dtcs(bool force);

    /// Read vehicle info (Mode $09: VIN, Cal ID, ECU name).
    [[nodiscard]] Result<VehicleInfo> read_vehicle_info();

private:
    IChannel& channel_;
    uint32_t tx_id_;
    uint32_t rx_base_;

    /// Send request and wait for single-frame response.
    [[nodiscard]] Result<ObdResponse>
    send_and_receive(uint8_t mode, uint8_t pid);

    /// Send request and collect multi-frame response (ISO-TP).
    [[nodiscard]] Result<std::vector<uint8_t>>
    send_and_receive_multiframe(uint8_t mode, uint8_t pid);

    /// Read frames from channel, filtering for OBD response IDs.
    [[nodiscard]] Result<CanFrame>
    read_obd_frame(uint32_t timeout_ms);
};

} // namespace canmatik
