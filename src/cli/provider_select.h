#pragma once

/// @file provider_select.h
/// Helper to pick a provider from a scanned list using --provider name match.

#include "transport/device_info.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace canmatik {

/// Find a provider by 1-based index or case-insensitive name substring.
/// Returns the matching DeviceInfo, or std::nullopt if no match.
inline std::optional<DeviceInfo> select_provider(
    const std::vector<DeviceInfo>& providers,
    const std::string& hint)
{
    if (hint.empty())
        return providers.empty() ? std::nullopt : std::optional(providers.front());

    // Try parsing as a 1-based index first (e.g. "1", "2")
    try {
        std::size_t pos = 0;
        unsigned long idx = std::stoul(hint, &pos);
        if (pos == hint.size() && idx >= 1 && idx <= providers.size())
            return providers[idx - 1];
    } catch (...) {
        // Not a number — fall through to name match
    }

    // Case-insensitive substring match
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    const std::string needle = lower(hint);
    for (const auto& dev : providers) {
        if (lower(dev.name).find(needle) != std::string::npos)
            return dev;
    }
    return std::nullopt;
}

} // namespace canmatik
