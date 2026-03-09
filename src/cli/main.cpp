/// @file main.cpp
/// CANmatik CLI entry point.

#include "cli/cli_app.h"

#include <CLI/CLI.hpp>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    canmatik::GlobalOptions globals;

    auto app = canmatik::build_cli(globals);

    // Parse CLI
    try {
        app->parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app->exit(e);
    }

    // Resolve configuration (file + CLI flags)
    auto config = canmatik::resolve_config(globals);

    // Initialize diagnostic logging
    canmatik::init_logging(config);

    // Dispatch to selected subcommand
    try {
        return canmatik::dispatch(*app, globals, config);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 2;
    }
}
