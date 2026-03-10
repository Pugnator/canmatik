/// @file cmd_scan.cpp
/// `canmatik scan` subcommand — discover and list J2534 providers (T031 — US1).
/// Output formats per contracts/cli-commands.md.

#include "cli/cli_app.h"
#include "platform/win32/j2534_provider.h"
#include "mock/mock_provider.h"
#include "transport/device_info.h"

#include "core/log_macros.h"
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace canmatik {

namespace {

/// Build the protocol support string for text output (e.g. "CAN ISO15765").
std::string protocol_string(const DeviceInfo& dev) {
    std::string result;
    if (dev.supports_can)      result += "CAN";
    if (dev.supports_iso15765) {
        if (!result.empty()) result += ' ';
        result += "ISO15765";
    }
    if (result.empty()) result = "-";
    return result;
}

/// Print the provider list in human-readable text format.
void print_text(const std::vector<DeviceInfo>& providers) {
    if (providers.empty()) {
        std::cout << "J2534 Providers:\n"
                  << "  (none found — install a J2534 driver or use --mock)\n";
        return;
    }

    std::cout << "J2534 Providers:\n";
    int idx = 1;
    for (const auto& dev : providers) {
        std::cout << "  " << idx << ". "
                  << dev.name;
        if (!dev.vendor.empty()) {
            std::cout << "    (" << dev.vendor << ')';
        }
        std::cout << "     " << protocol_string(dev)
                  << '\n';
        ++idx;
    }
}

/// Print the provider list as a single JSON object.
void print_json(const std::vector<DeviceInfo>& providers) {
    nlohmann::json root;
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& dev : providers) {
        nlohmann::json j;
        j["name"]              = dev.name;
        j["vendor"]            = dev.vendor;
        j["dll_path"]          = dev.dll_path;
        j["supports_can"]      = dev.supports_can;
        j["supports_iso15765"] = dev.supports_iso15765;
        arr.push_back(std::move(j));
    }

    root["providers"] = std::move(arr);
    std::cout << root.dump() << '\n';
}

} // anonymous namespace

int dispatch_scan(CLI::App& /*sub*/, const GlobalOptions& globals, const Config& /*config*/) {
    LOG_DEBUG("dispatch_scan: mock={}, json={}, verbose={}",
              globals.mock, globals.json, globals.verbose);

    // Select provider backend
    std::unique_ptr<IDeviceProvider> provider;
    if (globals.mock) {
        LOG_INFO("Using MockProvider for scan");
        provider = std::make_unique<MockProvider>();
    } else {
        LOG_INFO("Using J2534Provider for scan");
        provider = std::make_unique<J2534Provider>();
    }

    // Enumerate devices — registry failure surfaces as an exception
    std::vector<DeviceInfo> providers;
    try {
        providers = provider->enumerate();
        LOG_INFO("Scan found {} provider(s)", providers.size());
    } catch (const std::exception& ex) {
        LOG_ERROR("Registry scan failed: {}", ex.what());
        if (globals.json) {
            nlohmann::json err;
            err["error"] = std::string("Registry scan failed: ") + ex.what();
            std::cerr << err.dump() << '\n';
        } else {
            std::cerr << "Error: Registry scan failed: " << ex.what() << '\n';
        }
        return 2; // Exit code 2: registry access failure
    }

    // Format output
    if (globals.json) {
        print_json(providers);
    } else {
        print_text(providers);
    }

    return 0;
}

} // namespace canmatik
