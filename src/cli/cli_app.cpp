/// @file cli_app.cpp
/// CLI11 application setup, subcommand registration, TinyLog initialization,
/// config resolution, and dispatch.

#include "cli/cli_app.h"
#include "config/config.h"

#include <CLI/CLI.hpp>
#include "core/log_macros.h"

#include <filesystem>
#include <iostream>
#include <format>

namespace canmatik {

// ---------------------------------------------------------------------------
// Forward declarations for subcommand registration (implemented in cmd_*.cpp)
// ---------------------------------------------------------------------------
void register_scan_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_monitor_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_record_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_replay_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_status_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_demo_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_label_command(CLI::App& app, const GlobalOptions& globals, const Config& config);
void register_obd_command(CLI::App& app, const GlobalOptions& globals, const Config& config);

// Dispatch functions (implemented in cmd_*.cpp)
int dispatch_scan(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_monitor(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_record(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_replay(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_status(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_demo(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_label(CLI::App& sub, const GlobalOptions& globals, const Config& config);
int dispatch_obd(CLI::App& sub, const GlobalOptions& globals, const Config& config);

// ---------------------------------------------------------------------------
// build_cli
// ---------------------------------------------------------------------------
std::unique_ptr<CLI::App> build_cli(GlobalOptions& globals) {
    auto app = std::make_unique<CLI::App>(
        "CANmatik — J2534 CAN Bus Scanner & Logger"
    );
    app->set_version_flag("--version", "0.1.0");
    app->require_subcommand(1);
    app->fallthrough();  // Allow global options (--mock, --json, etc.) after subcommand name

    // Global options
    app->add_option("--config", globals.config_path,
        "Path to configuration file")->default_val("canmatik.json");
    app->add_option("--provider", globals.provider,
        "J2534 provider name");
    app->add_flag("--mock", globals.mock,
        "Use mock backend instead of real hardware");
    app->add_flag("--json", globals.json,
        "Output in JSON Lines format (machine-readable)");
    app->add_flag("--verbose", globals.verbose,
        "Enable verbose diagnostic output");
    app->add_flag("--debug", globals.debug,
        "Enable debug-level logging to file and stderr");

    // Subcommands — registered as stubs; dispatch routes after parsing
    app->add_subcommand("scan", "Discover and list installed J2534 providers");

    auto* monitor = app->add_subcommand("monitor", "Connect and display CAN frames in real time");
    monitor->add_option("--bitrate", "CAN bitrate in bps")->default_val("500000");
    monitor->add_option("--filter", "Filter specification (repeatable)")->expected(-1);

    auto* record = app->add_subcommand("record", "Monitor and record traffic to a log file");
    record->add_option("--bitrate", "CAN bitrate in bps")->default_val("500000");
    record->add_option("--output,-o", "Output file path (auto-generated if omitted)");
    record->add_option("--format,-f", "Log format: asc, jsonl")->default_val("asc");
    record->add_option("--filter", "Filter specification (repeatable)")->expected(-1);
    auto* replay = app->add_subcommand("replay", "Open and inspect a previously captured log");
    replay->add_option("file", "Path to log file (.asc or .jsonl)")->required();
    replay->add_option("--filter", "Filter specification (repeatable)")->expected(-1);
    replay->add_option("--search", "Search for frames with this arbitration ID (hex)");
    replay->add_flag("--summary", "Show session summary and ID distribution");
    app->add_subcommand("status", "Show current session / adapter information");
    auto* demo = app->add_subcommand("demo", "Run with mock backend (simulated traffic)");
    demo->add_option("--bitrate", "CAN bitrate in bps")->default_val("500000");
    demo->add_option("--frame-rate", "Simulated frames per second")->default_val("100");
    demo->add_option("--trace", "Replay a trace file (.asc or .jsonl) as mock input");
    demo->add_option("--filter", "Filter specification (repeatable)")->expected(-1);
    auto* label = app->add_subcommand("label", "Manage arbitration ID labels");
    auto* label_set = label->add_subcommand("set", "Set a label for an arb ID");
    label_set->add_option("id", "Arbitration ID (hex, e.g. 0x7E8)")->required();
    label_set->add_option("name", "Label name")->required();
    auto* label_remove = label->add_subcommand("remove", "Remove a label");
    label_remove->add_option("id", "Arbitration ID (hex)")->required();
    label->add_subcommand("list", "List all labels");
    label->require_subcommand(1);

    // OBD-II diagnostics
    auto* obd = app->add_subcommand("obd", "OBD-II diagnostics (J1979 modes, PID decoding, DTCs)");
    obd->add_option("--bitrate", "CAN bitrate in bps")->default_val("500000");
    obd->require_subcommand(1);

    auto* obd_query = obd->add_subcommand("query", "Query supported PIDs from ECU");
    obd_query->add_flag("--supported", "List all supported PIDs");

    auto* obd_stream = obd->add_subcommand("stream", "Stream decoded PID values in real time");
    obd_stream->add_option("--obd-config", "Path to OBD YAML config file");
    obd_stream->add_option("--interval", "Query interval override (e.g., 500ms, 1s, 2hz)");

    auto* obd_dtc = obd->add_subcommand("dtc", "Read or clear diagnostic trouble codes");
    obd_dtc->add_flag("--clear", "Clear stored DTCs (Mode $04)");
    obd_dtc->add_flag("--force", "Skip confirmation for --clear");

    obd->add_subcommand("info", "Read vehicle info (VIN, ECU name, calibration)");

    return app;
}

// ---------------------------------------------------------------------------
// init_logging — configure TinyLog based on resolved Config
// ---------------------------------------------------------------------------
void init_logging(const Config& config) {
    auto& logger = Log::get();

    // Reset and set base severity levels: info + warning + error + critical
    logger.reset_levels();
    logger.set_level(TraceSeverity::info)
          .set_level(TraceSeverity::warning)
          .set_level(TraceSeverity::error)
          .set_level(TraceSeverity::critical);

    if (config.verbose) {
        logger.set_level(TraceSeverity::verbose);
    }

    if (config.debug) {
        logger.set_level(TraceSeverity::debug)
              .set_level(TraceSeverity::verbose);
    }

    // Console tracer (always active)
    logger.configure(TraceType::console);

    // File tracer (only when --debug is set)
    if (config.debug) {
        RotationConfig rotation;
        rotation.max_file_size    = config.log_max_file_size;
        rotation.max_backup_count = config.log_max_backups;
        rotation.compress         = config.log_compress;
        logger.configure(TraceType::file, config.log_file, rotation);
    }

    LOG_INFO("CANmatik v0.1.0 starting");
    if (config.debug) {
        LOG_DEBUG("Debug mode enabled — logging to {}", config.log_file);
    }
}

// ---------------------------------------------------------------------------
// resolve_config — load JSON file then merge CLI flags
// ---------------------------------------------------------------------------
Config resolve_config(const GlobalOptions& globals) {
    Config config = default_config();
    Config file_config = default_config();

    // Try loading config file
    if (std::filesystem::exists(globals.config_path)) {
        auto err = file_config.load_from_file(globals.config_path);
        if (!err.empty()) {
            std::cerr << std::format("Warning: {}\n", err);
            // Continue with defaults
        } else {
            config = file_config;
        }
    }

    // Apply CLI overrides
    Config cli;
    cli.provider = globals.provider;
    cli.mock_enabled = globals.mock;
    cli.verbose = globals.verbose;
    cli.debug = globals.debug;

    Config defaults = default_config();
    config.merge_cli_flags(cli, defaults);

    return config;
}

// ---------------------------------------------------------------------------
// dispatch — route to the selected subcommand handler
// ---------------------------------------------------------------------------
int dispatch(CLI::App& app, const GlobalOptions& globals, const Config& config) {
    for (auto* sub : app.get_subcommands()) {
        if (sub->parsed()) {
            const auto& name = sub->get_name();
            if (name == "scan")    return dispatch_scan(*sub, globals, config);
            if (name == "monitor") return dispatch_monitor(*sub, globals, config);
            if (name == "record")  return dispatch_record(*sub, globals, config);
            if (name == "replay")  return dispatch_replay(*sub, globals, config);
            if (name == "status")  return dispatch_status(*sub, globals, config);
            if (name == "demo")    return dispatch_demo(*sub, globals, config);
            if (name == "label")   return dispatch_label(*sub, globals, config);
            if (name == "obd")     return dispatch_obd(*sub, globals, config);
        }
    }

    std::cerr << "Error: No subcommand selected. Run with --help for usage.\n";
    return 1;
}

} // namespace canmatik
