#pragma once

/// @file config.h
/// Application configuration: JSON loading, CLI-flag merge, TinyLog rotation settings.

#include <cstdint>
#include <string>
#include <vector>

#include "core/filter.h"
#include "core/session_status.h"

namespace canmatik {

/// Log format for recording.
enum class LogFormat {
    ASC,    ///< Vector ASC text format (.asc)
    JSONL,  ///< JSON Lines (.jsonl)
    CSV,    ///< Comma-separated values (.csv) — future
};

/// Parse a format string ("asc", "jsonl") into a LogFormat enum.
/// Returns true on success.
[[nodiscard]] bool parse_log_format(const std::string& s, LogFormat& out);

/// Return file extension for a log format (without dot).
[[nodiscard]] const char* log_format_extension(LogFormat fmt);

/// Application configuration. Loaded from file, overridden by CLI flags.
struct Config {
    // --- Connection ---
    std::string provider;             ///< J2534 provider name ("" = auto-select first)
    uint32_t bitrate = 500000;        ///< CAN bitrate in bps
    OperatingMode mode = OperatingMode::Passive; ///< Startup operating mode

    // --- Filters ---
    std::vector<FilterRule> filters;  ///< Startup filters

    // --- Output ---
    LogFormat output_format = LogFormat::ASC; ///< Default recording format
    std::string output_directory = "./captures"; ///< Default recording directory

    // --- GUI ---
    bool gui_launch = false;          ///< Whether to launch GUI by default
    uint32_t gui_font_size = 14;      ///< ImGui font size

    // --- Mock ---
    bool mock_enabled = false;        ///< Use mock backend
    uint32_t mock_frame_rate = 100;   ///< Frames/sec in mock mode
    std::string mock_trace_file;      ///< Replay this file as mock input

    // --- Diagnostic logging (TinyLog) ---
    bool verbose = false;             ///< Enable verbose diagnostic output
    bool debug = false;               ///< Enable debug-level file logging
    std::string log_file = "canmatik.log"; ///< Diagnostic log file path
    uint32_t log_max_file_size = 10485760; ///< Max log file size before rotation (bytes)
    uint32_t log_max_backups = 5;     ///< Max rotated backup files
    bool log_compress = true;         ///< Compress rotated logs with zstd

    /// Load configuration from a JSON file. Missing keys keep their defaults.
    /// Returns empty string on success, or error description.
    [[nodiscard]] std::string load_from_file(const std::string& path);

    /// Merge defaults: CLI flags override config file values.
    /// Call after load_from_file and CLI parsing.
    /// Each field is overridden only if the CLI explicitly set it.
    void merge_cli_flags(const Config& cli_overrides, const Config& defaults);
};

/// Return a Config with built-in defaults (no file, no CLI flags).
[[nodiscard]] Config default_config();

} // namespace canmatik
