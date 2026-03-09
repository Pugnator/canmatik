/// @file cmd_status.cpp
/// `canmatik status` subcommand — show provider/adapter information (T055 — US6).

#include "cli/cli_app.h"
#include "core/log_macros.h"
#include "mock/mock_provider.h"
#include "platform/win32/j2534_provider.h"
#include "services/session_service.h"

#include <nlohmann/json.hpp>

#include <format>
#include <iostream>

namespace canmatik {

int dispatch_status(CLI::App& /*sub*/, const GlobalOptions& globals, const Config& /*config*/) {
    LOG_DEBUG("dispatch_status: mock={}, json={}", globals.mock, globals.json);

    // --- Select provider ---
    SessionService session;
    if (globals.mock) {
        session.setProvider(std::make_unique<MockProvider>());
    } else {
        session.setProvider(std::make_unique<J2534Provider>());
    }

    // --- Enumerate providers ---
    auto devices = session.scan();

    if (globals.json) {
        nlohmann::json j;
        j["provider_count"] = devices.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& dev : devices) {
            nlohmann::json d;
            d["name"]     = dev.name;
            d["vendor"]   = dev.vendor;
            d["dll_path"] = dev.dll_path;
            d["can"]      = dev.supports_can;
            d["iso15765"] = dev.supports_iso15765;
            d["reachable"] = true;  // If scan returned it, it's reachable
            arr.push_back(d);
        }
        j["providers"] = arr;
        std::cout << j.dump(2) << '\n';
    } else {
        if (devices.empty()) {
            std::cout << "No J2534 providers found.";
            if (!globals.mock) {
                std::cout << " Use --mock for simulated status.";
            }
            std::cout << '\n';
            return 0;
        }

        std::cout << std::format("Found {} provider(s):\n", devices.size());
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& dev = devices[i];
            std::cout << std::format("  [{}] {}\n", i + 1, dev.name);
            if (!dev.vendor.empty())
                std::cout << std::format("      Vendor:   {}\n", dev.vendor);
            if (!dev.dll_path.empty())
                std::cout << std::format("      DLL:      {}\n", dev.dll_path);
            std::cout << std::format("      CAN:      {}\n", dev.supports_can ? "Yes" : "No");
            std::cout << std::format("      ISO15765: {}\n", dev.supports_iso15765 ? "Yes" : "No");
            std::cout << std::format("      Status:   Reachable\n");
        }
    }

    return 0;
}

} // namespace canmatik
