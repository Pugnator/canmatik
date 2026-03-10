/// @file config.cpp
/// Config JSON loading, merge logic, and format helpers.

#include "config/config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <format>

namespace canmatik {

bool parse_log_format(const std::string& s, LogFormat& out) {
    if (s == "asc")   { out = LogFormat::ASC;   return true; }
    if (s == "jsonl")  { out = LogFormat::JSONL;  return true; }
    if (s == "csv")    { out = LogFormat::CSV;    return true; }
    return false;
}

const char* log_format_extension(LogFormat fmt) {
    switch (fmt) {
        case LogFormat::ASC:   return "asc";
        case LogFormat::JSONL: return "jsonl";
        case LogFormat::CSV:   return "csv";
    }
    return "bin";
}

Config default_config() {
    return Config{};
}

std::string Config::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::format("Cannot open config file: {}", path);
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return std::format("Config JSON parse error: {}", e.what());
    }

    // --- Connection ---
    if (j.contains("provider") && j["provider"].is_string()) {
        provider = j["provider"].get<std::string>();
    }
    if (j.contains("bitrate") && j["bitrate"].is_number_unsigned()) {
        bitrate = j["bitrate"].get<uint32_t>();
    }
    if (j.contains("mode") && j["mode"].is_string()) {
        auto m = j["mode"].get<std::string>();
        if (m == "passive")       mode = OperatingMode::Passive;
        else if (m == "active_query")  mode = OperatingMode::ActiveQuery;
        else if (m == "active_inject") mode = OperatingMode::ActiveInject;
    }

    // --- Filters ---
    if (j.contains("filters") && j["filters"].is_array()) {
        for (const auto& fj : j["filters"]) {
            FilterRule rule;
            if (fj.contains("action") && fj["action"].is_string()) {
                rule.action = (fj["action"] == "block") ?
                    FilterAction::Block : FilterAction::Pass;
            }
            if (fj.contains("id_value") && fj["id_value"].is_number_unsigned()) {
                rule.id_value = fj["id_value"].get<uint32_t>();
            }
            if (fj.contains("id_mask") && fj["id_mask"].is_number_unsigned()) {
                rule.id_mask = fj["id_mask"].get<uint32_t>();
            }
            filters.push_back(rule);
        }
    }

    // --- Output (nested) ---
    if (j.contains("output") && j["output"].is_object()) {
        const auto& oj = j["output"];
        if (oj.contains("format") && oj["format"].is_string()) {
            parse_log_format(oj["format"].get<std::string>(), output_format);
        }
        if (oj.contains("directory") && oj["directory"].is_string()) {
            output_directory = oj["directory"].get<std::string>();
        }
    }

    // --- GUI (nested) ---
    if (j.contains("gui") && j["gui"].is_object()) {
        const auto& gj = j["gui"];
        if (gj.contains("launch") && gj["launch"].is_boolean()) {
            gui_launch = gj["launch"].get<bool>();
        }
        if (gj.contains("font_size") && gj["font_size"].is_number_unsigned()) {
            gui_font_size = gj["font_size"].get<uint32_t>();
        }
    }

    // --- Mock (nested) ---
    if (j.contains("mock") && j["mock"].is_object()) {
        const auto& mj = j["mock"];
        if (mj.contains("enabled") && mj["enabled"].is_boolean()) {
            mock_enabled = mj["enabled"].get<bool>();
        }
        if (mj.contains("frame_rate") && mj["frame_rate"].is_number_unsigned()) {
            mock_frame_rate = mj["frame_rate"].get<uint32_t>();
        }
        if (mj.contains("trace_file") && mj["trace_file"].is_string()) {
            mock_trace_file = mj["trace_file"].get<std::string>();
        }
    }

    // --- Logging / TinyLog (nested) ---
    if (j.contains("logging") && j["logging"].is_object()) {
        const auto& lj = j["logging"];
        if (lj.contains("file") && lj["file"].is_string()) {
            log_file = lj["file"].get<std::string>();
        }
        if (lj.contains("max_file_size") && lj["max_file_size"].is_number_unsigned()) {
            log_max_file_size = lj["max_file_size"].get<uint32_t>();
        }
        if (lj.contains("max_backups") && lj["max_backups"].is_number_unsigned()) {
            log_max_backups = lj["max_backups"].get<uint32_t>();
        }
        if (lj.contains("compress") && lj["compress"].is_boolean()) {
            log_compress = lj["compress"].get<bool>();
        }
    }

    return {};
}

void Config::merge_cli_flags(const Config& cli, const Config& defaults) {
    // Only override fields where CLI differs from defaults (meaning user set it)
    if (cli.provider != defaults.provider)           provider = cli.provider;
    if (cli.bitrate != defaults.bitrate)             bitrate = cli.bitrate;
    if (cli.mode != defaults.mode)                   mode = cli.mode;
    if (!cli.filters.empty())                        filters = cli.filters;
    if (cli.output_format != defaults.output_format) output_format = cli.output_format;
    if (cli.output_directory != defaults.output_directory) output_directory = cli.output_directory;
    if (cli.mock_enabled != defaults.mock_enabled)   mock_enabled = cli.mock_enabled;
    if (cli.mock_frame_rate != defaults.mock_frame_rate) mock_frame_rate = cli.mock_frame_rate;
    if (cli.mock_trace_file != defaults.mock_trace_file) mock_trace_file = cli.mock_trace_file;
    if (cli.verbose != defaults.verbose)             verbose = cli.verbose;
    if (cli.debug != defaults.debug)                 debug = cli.debug;
    if (cli.log_file != defaults.log_file)           log_file = cli.log_file;
}

} // namespace canmatik
