/// @file test_can_frame.cpp
/// Unit tests for CanFrame, FrameType, and validation (T021).

#include <catch2/catch_test_macros.hpp>
#include "core/can_frame.h"

using namespace canmatik;

// ---------------------------------------------------------------------------
// CanFrame default construction
// ---------------------------------------------------------------------------
TEST_CASE("CanFrame default construction", "[can_frame]") {
    CanFrame f;
    CHECK(f.adapter_timestamp_us == 0);
    CHECK(f.host_timestamp_us == 0);
    CHECK(f.arbitration_id == 0);
    CHECK(f.type == FrameType::Standard);
    CHECK(f.dlc == 0);
    CHECK(f.channel_id == 0);
    for (auto b : f.data) {
        CHECK(b == 0);
    }
}

// ---------------------------------------------------------------------------
// FrameType values
// ---------------------------------------------------------------------------
TEST_CASE("FrameType enum values", "[can_frame]") {
    CHECK(static_cast<uint8_t>(FrameType::Standard) == 0);
    CHECK(static_cast<uint8_t>(FrameType::Extended) == 1);
    CHECK(static_cast<uint8_t>(FrameType::FD) == 2);
    CHECK(static_cast<uint8_t>(FrameType::Error) == 3);
    CHECK(static_cast<uint8_t>(FrameType::Remote) == 4);
}

// ---------------------------------------------------------------------------
// frame_type_to_string
// ---------------------------------------------------------------------------
TEST_CASE("frame_type_to_string", "[can_frame]") {
    CHECK(std::string(frame_type_to_string(FrameType::Standard)) == "Std");
    CHECK(std::string(frame_type_to_string(FrameType::Extended)) == "Ext");
    CHECK(std::string(frame_type_to_string(FrameType::FD))       == "FD");
    CHECK(std::string(frame_type_to_string(FrameType::Error))    == "Err");
    CHECK(std::string(frame_type_to_string(FrameType::Remote))   == "Rtr");
}

// ---------------------------------------------------------------------------
// is_valid_id
// ---------------------------------------------------------------------------
TEST_CASE("is_valid_id — standard frame", "[can_frame]") {
    CHECK(is_valid_id(0x000, FrameType::Standard));
    CHECK(is_valid_id(0x7FF, FrameType::Standard));
    CHECK_FALSE(is_valid_id(0x800, FrameType::Standard));
    CHECK_FALSE(is_valid_id(0xFFFFFFFF, FrameType::Standard));
}

TEST_CASE("is_valid_id — extended frame", "[can_frame]") {
    CHECK(is_valid_id(0x000, FrameType::Extended));
    CHECK(is_valid_id(0x7FF, FrameType::Extended));
    CHECK(is_valid_id(0x800, FrameType::Extended));
    CHECK(is_valid_id(0x1FFFFFFF, FrameType::Extended));
    CHECK_FALSE(is_valid_id(0x20000000, FrameType::Extended));
}

TEST_CASE("is_valid_id — FD frame uses extended range", "[can_frame]") {
    CHECK(is_valid_id(0x1FFFFFFF, FrameType::FD));
    CHECK_FALSE(is_valid_id(0x20000000, FrameType::FD));
}

// ---------------------------------------------------------------------------
// validate_frame — valid frames
// ---------------------------------------------------------------------------
TEST_CASE("validate_frame — valid standard frame", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Standard;
    f.arbitration_id = 0x7E8;
    f.dlc = 8;
    f.data = {0x02, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00};
    CHECK(validate_frame(f).empty());
}

TEST_CASE("validate_frame — valid extended frame", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Extended;
    f.arbitration_id = 0x18DAF110;
    f.dlc = 3;
    f.data = {0x02, 0x01, 0x00};
    CHECK(validate_frame(f).empty());
}

TEST_CASE("validate_frame — zero DLC is valid", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Standard;
    f.arbitration_id = 0x100;
    f.dlc = 0;
    CHECK(validate_frame(f).empty());
}

// ---------------------------------------------------------------------------
// validate_frame — invalid frames
// ---------------------------------------------------------------------------
TEST_CASE("validate_frame — standard ID too large", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Standard;
    f.arbitration_id = 0x800; // exceeds 0x7FF
    f.dlc = 1;
    f.data[0] = 0xFF;
    auto err = validate_frame(f);
    CHECK_FALSE(err.empty());
    CHECK(err.find("exceeds max") != std::string::npos);
}

TEST_CASE("validate_frame — extended ID too large", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Extended;
    f.arbitration_id = 0x20000000;
    f.dlc = 0;
    auto err = validate_frame(f);
    CHECK_FALSE(err.empty());
}

TEST_CASE("validate_frame — DLC exceeds classic max", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Standard;
    f.arbitration_id = 0x100;
    f.dlc = 9; // max is 8 for classic CAN
    auto err = validate_frame(f);
    CHECK_FALSE(err.empty());
    CHECK(err.find("DLC") != std::string::npos);
}

TEST_CASE("validate_frame — non-zero trailing bytes", "[can_frame]") {
    CanFrame f;
    f.type = FrameType::Standard;
    f.arbitration_id = 0x100;
    f.dlc = 2;
    f.data[0] = 0xAA;
    f.data[1] = 0xBB;
    f.data[2] = 0xCC; // beyond DLC — should be zero
    auto err = validate_frame(f);
    CHECK_FALSE(err.empty());
    CHECK(err.find("Non-zero byte") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
TEST_CASE("CAN constants", "[can_frame]") {
    CHECK(kClassicCanMaxDlc == 8);
    CHECK(kCanFdMaxDlc == 64);
    CHECK(kMaxStandardId == 0x7FFu);
    CHECK(kMaxExtendedId == 0x1FFFFFFFu);
}
