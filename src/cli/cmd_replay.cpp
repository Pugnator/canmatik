/// @file cmd_replay.cpp
/// `canmatik replay` subcommand — open and inspect a previously captured log (T053 — US5).

#include "cli/cli_app.h"
#include "cli/formatters.h"
#include "core/filter.h"
#include "core/log_macros.h"
#include "services/replay_service.h"

#include <nlohmann/json.hpp>

#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace canmatik {

int dispatch_replay(CLI::App& sub, const GlobalOptions& globals, const Config& /*config*/) {
    LOG_DEBUG("dispatch_replay: json={}", globals.json);

    // --- Parse replay-specific options ---
    std::string file_path;
    std::vector<std::string> filter_specs;
    std::string search_id_str;
    bool show_summary = false;

    if (auto* opt = sub.get_option_no_throw("file")) {
        if (opt->count() > 0) file_path = opt->as<std::string>();
    }
    if (auto* opt = sub.get_option_no_throw("--filter")) {
        if (opt->count() > 0) filter_specs = opt->as<std::vector<std::string>>();
    }
    if (auto* opt = sub.get_option_no_throw("--search")) {
        if (opt->count() > 0) search_id_str = opt->as<std::string>();
    }
    if (auto* opt = sub.get_option_no_throw("--summary")) {
        if (opt->count() > 0) show_summary = true;
    }

    if (file_path.empty()) {
        std::cerr << "Error: No file specified. Usage: canmatik replay <file>\n";
        return 1;
    }

    // --- Load file ---
    ReplayService replay;
    if (!replay.load(file_path)) {
        std::cerr << "Error: Failed to open log file: " << file_path << '\n';
        return 1;
    }

    // --- Build filter engine ---
    FilterEngine filter;
    for (const auto& spec : filter_specs) {
        FilterRule rule;
        auto err = parse_filter(spec, rule);
        if (!err.empty()) {
            std::cerr << "Error: Invalid filter '" << spec << "': " << err << '\n';
            return 1;
        }
        filter.add_rule(rule);
    }

    // --- Handle --search mode ---
    if (!search_id_str.empty()) {
        // Parse search ID (supports 0x prefix)
        uint32_t search_id = 0;
        std::string_view sv(search_id_str);
        if (sv.starts_with("0x") || sv.starts_with("0X")) sv.remove_prefix(2);
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), search_id, 16);
        if (ec != std::errc{}) {
            std::cerr << "Error: Invalid search ID '" << search_id_str << "'\n";
            return 1;
        }

        auto matches = replay.search(search_id);
        if (globals.json) {
            for (const auto& frame : matches) {
                if (!filter.rules().empty() && !filter.evaluate(frame.arbitration_id)) continue;
                std::cout << format_frame_json(frame, 0) << '\n';
            }
        } else {
            std::cout << std::format("Search 0x{:X}: {} matches in {}\n",
                                     search_id, matches.size(), file_path);
            for (const auto& frame : matches) {
                if (!filter.rules().empty() && !filter.evaluate(frame.arbitration_id)) continue;
                std::cout << format_frame_text(frame, 0) << '\n';
            }
        }
        return 0;
    }

    // --- Handle --summary mode ---
    if (show_summary) {
        auto s = replay.summary();

        if (globals.json) {
            nlohmann::json j;
            j["file"]          = file_path;
            j["total_frames"]  = s.total_frames;
            j["unique_ids"]    = s.unique_ids;
            j["duration"]      = s.duration_seconds;
            j["adapter"]       = s.adapter_name;
            j["bitrate"]       = s.bitrate;
            nlohmann::json dist;
            for (auto& [id, cnt] : s.id_distribution) {
                dist[std::format("{:X}", id)] = cnt;
            }
            j["id_distribution"] = dist;
            std::cout << j.dump(2) << '\n';
        } else {
            std::cout << std::format("File: {}\n", file_path);
            if (!s.adapter_name.empty())
                std::cout << std::format("Adapter: {}\n", s.adapter_name);
            if (s.bitrate > 0)
                std::cout << std::format("Bitrate: {} kbps\n", s.bitrate / 1000);
            std::cout << std::format("Frames: {}\n", s.total_frames);
            std::cout << std::format("Unique IDs: {}\n", s.unique_ids);
            std::cout << std::format("Duration: {:.3f} s\n", s.duration_seconds);
            std::cout << "ID Distribution:\n";
            for (auto& [id, cnt] : s.id_distribution) {
                std::cout << std::format("  0x{:03X}: {} frames\n", id, cnt);
            }
        }
        return 0;
    }

    // --- Default mode: display all frames ---
    auto& frames = replay.frames();
    for (const auto& frame : frames) {
        if (!filter.rules().empty() && !filter.evaluate(frame.arbitration_id)) continue;

        if (globals.json) {
            std::cout << format_frame_json(frame, 0) << '\n';
        } else {
            std::cout << format_frame_text(frame, 0) << '\n';
        }
    }

    // Print footer summary
    auto meta = replay.metadata();
    meta.frames_received = frames.size();
    print_session_footer(meta, globals.json);

    return 0;
}

} // namespace canmatik
