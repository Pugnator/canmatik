#pragma once

/// @file obd_request.h
/// Build ISO 15765-4 CAN frames for OBD-II requests.

#include <cstring>

#include "core/can_frame.h"
#include "obd/iso15765.h"

namespace canmatik {

/// Build a single-frame OBD-II request CAN frame.
/// Data[0] = PCI (0x02 = single frame, 2 payload bytes)
/// Data[1] = mode, Data[2] = pid, Data[3..7] = 0x55 padding.
[[nodiscard]] inline CanFrame build_obd_request(uint8_t mode, uint8_t pid,
                                                 uint32_t tx_id = iso15765::kFunctionalTxId) {
    CanFrame frame{};
    frame.arbitration_id = tx_id;
    frame.type = FrameType::Standard;
    frame.dlc = iso15765::kStandardDlc;

    frame.data.fill(iso15765::kPaddingByte);
    frame.data[0] = 0x02; // Single frame, 2 payload bytes
    frame.data[1] = mode;
    frame.data[2] = pid;

    return frame;
}

/// Build an ISO-TP flow control frame (for multi-frame responses like VIN).
[[nodiscard]] inline CanFrame build_flow_control(uint32_t tx_id = iso15765::kFunctionalTxId) {
    CanFrame frame{};
    frame.arbitration_id = tx_id;
    frame.type = FrameType::Standard;
    frame.dlc = iso15765::kStandardDlc;

    frame.data.fill(iso15765::kPaddingByte);
    frame.data[0] = iso15765::kFlowControlContinue;
    frame.data[1] = iso15765::kFlowControlBlockSize;
    frame.data[2] = iso15765::kFlowControlSTmin;

    return frame;
}

} // namespace canmatik
