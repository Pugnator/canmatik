/// @file test_config.cpp
/// Unit tests for Config loading, merging, and format helpers (T024).

#include <catch2/catch_test_macros.hpp>
#include "config/config.h"

#include <fstream>
#include <filesystem>

using namespace canmatik;

// ---------------------------------------------------------------------------
// LogFormat helpers
// ---------------------------------------------------------------------------
TEST_CASE("parse_log_format valid strings", "[config]") {
    LogFormat fmt{};
    REQUIRE(parse_log_format("asc", fmt));
    CHECK(fmt == LogFormat::ASC);

    REQUIRE(parse_log_format("jsonl", fmt));
    CHECK(fmt == LogFormat::JSONL);

    REQUIRE(parse_log_format("csv", fmt));
    CHECK(fmt == LogFormat::CSV);
}

TEST_CASE("parse_log_format rejects invalid string", "[config]") {
    LogFormat fmt = LogFormat::ASC;
    CHECK_FALSE(parse_log_format("xml", fmt));
    // fmt unchanged
    CHECK(fmt == LogFormat::ASC);
}

TEST_CASE("log_format_extension returns correct extension", "[config]") {
    CHECK(std::string(log_format_extension(LogFormat::ASC))   == "asc");
    CHECK(std::string(log_format_extension(LogFormat::JSONL))  == "jsonl");
    CHECK(std::string(log_format_extension(LogFormat::CSV))    == "csv");
}

// ---------------------------------------------------------------------------
// default_config()
// ---------------------------------------------------------------------------
TEST_CASE("default_config has expected values", "[config]") {
    Config c = default_config();
    CHECK(c.provider.empty());
    CHECK(c.bitrate == 500000);
    CHECK(c.mode == OperatingMode::Passive);
    CHECK(c.filters.empty());
    CHECK(c.output_format == LogFormat::ASC);
    CHECK(c.output_directory == "./captures");
    CHECK(c.gui_launch == false);
    CHECK(c.gui_font_size == 14);
    CHECK(c.mock_enabled == false);
    CHECK(c.mock_frame_rate == 100);
    CHECK(c.mock_trace_file.empty());
    CHECK(c.verbose == false);
    CHECK(c.debug == false);
    CHECK(c.log_file == "canmatik.log");
    CHECK(c.log_max_file_size == 10485760);
    CHECK(c.log_max_backups == 5);
    CHECK(c.log_compress == true);
}

// ---------------------------------------------------------------------------
// load_from_file — valid JSON
// ---------------------------------------------------------------------------
TEST_CASE("Config loads valid JSON with all sections", "[config]") {
    const char* json = R"({
        "provider": "Tactrix",
        "bitrate": 250000,
        "mode": "active_query",
        "filters": [
            { "action": "pass",  "id_value": 2024, "id_mask": 4294967295 },
            { "action": "block", "id_value": 0 }
        ],
        "output": {
            "format": "jsonl",
            "directory": "/tmp/logs"
        },
        "gui": {
            "launch": true,
            "font_size": 18
        },
        "mock": {
            "enabled": true,
            "frame_rate": 500,
            "trace_file": "demo.asc"
        },
        "logging": {
            "file": "test.log",
            "max_file_size": 1048576,
            "max_backups": 3,
            "compress": false
        }
    })";

    const std::string path = "test_config_full.json";
    {
        std::ofstream f(path);
        f << json;
    }

    Config c = default_config();
    auto err = c.load_from_file(path);
    std::filesystem::remove(path);

    REQUIRE(err.empty());

    CHECK(c.provider == "Tactrix");
    CHECK(c.bitrate == 250000);
    CHECK(c.mode == OperatingMode::ActiveQuery);
    REQUIRE(c.filters.size() == 2);
    CHECK(c.filters[0].action == FilterAction::Pass);
    CHECK(c.filters[0].id_value == 2024);
    CHECK(c.filters[0].id_mask == 0xFFFFFFFF);
    CHECK(c.filters[1].action == FilterAction::Block);

    CHECK(c.output_format == LogFormat::JSONL);
    CHECK(c.output_directory == "/tmp/logs");

    CHECK(c.gui_launch == true);
    CHECK(c.gui_font_size == 18);

    CHECK(c.mock_enabled == true);
    CHECK(c.mock_frame_rate == 500);
    CHECK(c.mock_trace_file == "demo.asc");

    CHECK(c.log_file == "test.log");
    CHECK(c.log_max_file_size == 1048576);
    CHECK(c.log_max_backups == 3);
    CHECK(c.log_compress == false);
}

// ---------------------------------------------------------------------------
// load_from_file — missing keys keep defaults
// ---------------------------------------------------------------------------
TEST_CASE("Config load keeps defaults for absent keys", "[config]") {
    const std::string path = "test_config_partial.json";
    {
        std::ofstream f(path);
        f << R"({ "bitrate": 125000 })";
    }

    Config c = default_config();
    auto err = c.load_from_file(path);
    std::filesystem::remove(path);

    REQUIRE(err.empty());
    CHECK(c.bitrate == 125000);
    // Everything else should remain default
    CHECK(c.provider.empty());
    CHECK(c.mode == OperatingMode::Passive);
    CHECK(c.output_format == LogFormat::ASC);
    CHECK(c.output_directory == "./captures");
    CHECK(c.mock_enabled == false);
    CHECK(c.verbose == false);
}

// ---------------------------------------------------------------------------
// load_from_file — invalid JSON
// ---------------------------------------------------------------------------
TEST_CASE("Config load returns error on invalid JSON", "[config]") {
    const std::string path = "test_config_bad.json";
    {
        std::ofstream f(path);
        f << "{ not valid json }";
    }

    Config c = default_config();
    auto err = c.load_from_file(path);
    std::filesystem::remove(path);

    CHECK_FALSE(err.empty());
    CHECK(err.find("parse error") != std::string::npos);
}

// ---------------------------------------------------------------------------
// load_from_file — non-existent file
// ---------------------------------------------------------------------------
TEST_CASE("Config load returns error for non-existent file", "[config]") {
    Config c = default_config();
    auto err = c.load_from_file("definitely_does_not_exist_42.json");
    CHECK_FALSE(err.empty());
    CHECK(err.find("Cannot open") != std::string::npos);
}

// ---------------------------------------------------------------------------
// merge_cli_flags
// ---------------------------------------------------------------------------
TEST_CASE("merge_cli_flags overrides only explicitly set fields", "[config]") {
    Config defaults = default_config();
    Config file_cfg = default_config();
    file_cfg.bitrate = 250000;
    file_cfg.output_directory = "/data";

    // Simulate CLI where user only set --mock and --verbose
    Config cli = defaults;
    cli.mock_enabled = true;
    cli.verbose = true;

    file_cfg.merge_cli_flags(cli, defaults);

    // File values preserved where CLI was default
    CHECK(file_cfg.bitrate == 250000);
    CHECK(file_cfg.output_directory == "/data");
    // CLI overrides applied
    CHECK(file_cfg.mock_enabled == true);
    CHECK(file_cfg.verbose == true);
}

TEST_CASE("merge_cli_flags: CLI provider overrides file provider", "[config]") {
    Config defaults = default_config();
    Config file_cfg = default_config();
    file_cfg.provider = "FileProvider";

    Config cli = defaults;
    cli.provider = "CLIProvider";

    file_cfg.merge_cli_flags(cli, defaults);
    CHECK(file_cfg.provider == "CLIProvider");
}

TEST_CASE("merge_cli_flags: default CLI leaves file values intact", "[config]") {
    Config defaults = default_config();
    Config file_cfg = default_config();
    file_cfg.provider = "Tactrix";
    file_cfg.bitrate = 1000000;
    file_cfg.output_format = LogFormat::JSONL;

    Config cli = defaults; // no changes

    file_cfg.merge_cli_flags(cli, defaults);

    CHECK(file_cfg.provider == "Tactrix");
    CHECK(file_cfg.bitrate == 1000000);
    CHECK(file_cfg.output_format == LogFormat::JSONL);
}
