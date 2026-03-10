#pragma once

/// @file iso15765.h
/// ISO 15765-4 constants and helpers for OBD-II CAN communication.

#include <cstdint>

namespace canmatik {
namespace iso15765 {

// Standard 11-bit CAN addressing
constexpr uint32_t kFunctionalTxId = 0x7DF;    // Functional broadcast request
constexpr uint32_t kPhysicalTxBase = 0x7E0;     // Physical request base (0x7E0–0x7E7)
constexpr uint32_t kResponseBase   = 0x7E8;     // Response base (0x7E8–0x7EF)
constexpr uint32_t kResponseEnd    = 0x7EF;     // Last valid response ID

// ISO-TP Protocol Control Information (PCI) byte types
constexpr uint8_t kPciSingleFrame       = 0x00; // Upper nibble = 0
constexpr uint8_t kPciFirstFrame        = 0x10; // Upper nibble = 1
constexpr uint8_t kPciConsecutiveFrame  = 0x20; // Upper nibble = 2
constexpr uint8_t kPciFlowControl       = 0x30; // Upper nibble = 3

// Flow control parameters
constexpr uint8_t kFlowControlContinue  = 0x30; // FC: continue to send
constexpr uint8_t kFlowControlBlockSize = 0x00; // Send all remaining frames
constexpr uint8_t kFlowControlSTmin     = 0x00; // No minimum separation time

// Padding
constexpr uint8_t kPaddingByte = 0x55;

// CAN DLC
constexpr uint8_t kStandardDlc = 8;

// Timeouts (milliseconds)
constexpr uint32_t kP2CanTimeout = 50;   // Normal response timeout
constexpr uint32_t kP2StarTimeout = 5000; // Extended response timeout (after 0x78)

// OBD-II Mode offset for positive response
constexpr uint8_t kPositiveResponseOffset = 0x40;

// Negative response service ID
constexpr uint8_t kNegativeResponse = 0x7F;

/// Check if a CAN ID is a valid OBD-II response ID (0x7E8–0x7EF).
[[nodiscard]] constexpr bool is_response_id(uint32_t id) {
    return id >= kResponseBase && id <= kResponseEnd;
}

/// Check if a CAN ID is a valid physical request ID (0x7E0–0x7E7).
[[nodiscard]] constexpr bool is_physical_request_id(uint32_t id) {
    return id >= kPhysicalTxBase && id < kResponseBase;
}

} // namespace iso15765
} // namespace canmatik
