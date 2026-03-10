#pragma once

/// @file cli_app.h
/// CLI11 application setup, subcommand registration, and TinyLog initialization.

#include "config/config.h"

#include <CLI/CLI.hpp>
#include <string>
#include <memory>

namespace canmatik {

/// Global CLI options shared across all subcommands.
struct GlobalOptions {
    std::string config_path = "canmatik.json";
    std::string provider;
    bool mock     = false;
    bool json     = false;
    bool verbose  = false;
    bool debug    = false;
};

/// Build and configure the CLI11 application with all subcommands.
/// @return The configured CLI::App ready for parsing.
std::unique_ptr<CLI::App> build_cli(GlobalOptions& globals);

/// Initialize TinyLog based on resolved configuration.
/// Call after CLI parsing and config file loading.
void init_logging(const Config& config);

/// Load configuration, merging file + CLI flags.
/// @param globals The parsed global CLI options.
/// @return Resolved Config struct.
Config resolve_config(const GlobalOptions& globals);

/// Run the selected subcommand. Returns process exit code.
int dispatch(CLI::App& app, const GlobalOptions& globals, const Config& config);

} // namespace canmatik
