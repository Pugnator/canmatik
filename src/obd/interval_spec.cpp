/// @file interval_spec.cpp
/// IntervalSpec parser implementation.

#include "obd/interval_spec.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

namespace canmatik {

Result<IntervalSpec> parse_interval(const std::string& input) {
    if (input.empty()) {
        return Result<IntervalSpec>::error("empty interval string");
    }

    // Lowercase copy for case-insensitive parsing
    std::string s = input;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Try to parse the numeric prefix
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr == s.data()) {
        return Result<IntervalSpec>::error("invalid interval: cannot parse number in '" + input + "'");
    }

    std::string suffix(ptr, static_cast<size_t>(s.data() + s.size() - ptr));

    uint32_t ms = 0;
    if (suffix == "ms") {
        ms = static_cast<uint32_t>(std::round(value));
    } else if (suffix == "s") {
        ms = static_cast<uint32_t>(std::round(value * 1000.0));
    } else if (suffix == "hz") {
        if (value <= 0.0) {
            return Result<IntervalSpec>::error("invalid interval: frequency must be > 0 in '" + input + "'");
        }
        ms = static_cast<uint32_t>(std::round(1000.0 / value));
    } else {
        return Result<IntervalSpec>::error("invalid interval: unknown suffix in '" + input
                               + "' (expected ms, s, or hz)");
    }

    if (ms < IntervalSpec::kMinMs) {
        return Result<IntervalSpec>::error("interval " + input + " resolves to " + std::to_string(ms)
                               + "ms, below minimum " + std::to_string(IntervalSpec::kMinMs) + "ms");
    }
    if (ms > IntervalSpec::kMaxMs) {
        return Result<IntervalSpec>::error("interval " + input + " resolves to " + std::to_string(ms)
                               + "ms, above maximum " + std::to_string(IntervalSpec::kMaxMs) + "ms");
    }

    return IntervalSpec{ms};
}

} // namespace canmatik
