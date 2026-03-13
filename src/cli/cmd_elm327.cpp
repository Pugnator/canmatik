/// @file cmd_elm327.cpp
/// `canmatik elm327` subcommand — run ELM327 serial-to-J2534 bridge.

#include "services/elm327_bridge.h"
#include "cli/cli_app.h"
#include "config/config.h"

#include <iostream>

namespace canmatik {

int dispatch_elm327(CLI::App& sub, const GlobalOptions& globals, const Config& config) {
    std::string serial;
    std::string provider;
    if (auto* opt = sub.get_option_no_throw("--serial")) {
        if (opt->count() > 0) serial = opt->as<std::string>();
    }
    if (auto* opt = sub.get_option_no_throw("--provider")) {
        if (opt->count() > 0) provider = opt->as<std::string>();
    }

    if (serial.empty()) {
        std::cerr << "Error: --serial COMx required\n";
        return 1;
    }

    if (provider.empty()) provider = config.provider;

    bool terminated = false;
    if (auto* opt = sub.get_option_no_throw("--terminated")) {
        if (opt->count() > 0) terminated = opt->as<bool>();
    }

    Elm327Bridge bridge(serial, provider, nullptr, terminated);
    return bridge.run();
}

} // namespace canmatik
