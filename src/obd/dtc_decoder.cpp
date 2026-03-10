/// @file dtc_decoder.cpp
/// DTC decoding implementation.

#include "obd/dtc_decoder.h"

#include <array>

namespace canmatik {

DtcCode decode_dtc(uint8_t byte0, uint8_t byte1, uint32_t ecu_id, bool pending) {
    static constexpr std::array<char, 4> kCategoryPrefix = {'P', 'C', 'B', 'U'};

    DtcCode dtc;
    dtc.ecu_id = ecu_id;
    dtc.raw = static_cast<uint16_t>((byte0 << 8) | byte1);
    dtc.pending = pending;

    uint8_t cat = (byte0 >> 6) & 0x03;
    dtc.category = static_cast<DtcCategory>(cat);

    char prefix = kCategoryPrefix[cat];
    uint8_t d2 = (byte0 >> 4) & 0x03;
    uint8_t d3 = byte0 & 0x0F;
    uint8_t d4 = (byte1 >> 4) & 0x0F;
    uint8_t d5 = byte1 & 0x0F;

    dtc.code.reserve(5);
    dtc.code += prefix;
    dtc.code += static_cast<char>('0' + d2);
    dtc.code += static_cast<char>('0' + d3);
    dtc.code += static_cast<char>('0' + d4);
    dtc.code += static_cast<char>('0' + d5);

    return dtc;
}

Result<std::vector<DtcCode>>
decode_dtcs(const uint8_t* data, size_t length, uint32_t ecu_id, bool pending) {
    if (length % 2 != 0) {
        return Result<std::vector<DtcCode>>::error("DTC data has odd byte count (" + std::to_string(length) + ")");
    }

    std::vector<DtcCode> result;
    result.reserve(length / 2);

    for (size_t i = 0; i < length; i += 2) {
        // Skip null DTCs (0x0000)
        if (data[i] == 0 && data[i + 1] == 0) continue;
        result.push_back(decode_dtc(data[i], data[i + 1], ecu_id, pending));
    }

    return result;
}

} // namespace canmatik
