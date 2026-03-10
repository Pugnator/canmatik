/// @file cmd_record.cpp
/// `canmatik record` subcommand — monitor and record traffic to a log file (T045 — US4).

#include "cli/cli_app.h"
#include "cli/formatters.h"
#include "cli/provider_select.h"
#include "core/capture_sink.h"
#include "core/filter.h"
#include "core/log_macros.h"
#include "core/timestamp.h"
#include "mock/mock_provider.h"
#include "platform/win32/j2534_provider.h"
#include "services/capture_service.h"
#include "services/record_service.h"
#include "services/session_service.h"
#include "transport/transport_error.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace canmatik {

namespace {

std::atomic<bool> g_record_stop_requested{false};

void record_signal_handler(int /*sig*/) {
    g_record_stop_requested.store(true);
}

/// Sink that prints frames to stdout (same as monitor).
class RecordConsoleSink : public ICaptureSync {
public:
    RecordConsoleSink(bool json_mode, uint64_t session_start_us)
        : json_mode_(json_mode), session_start_us_(session_start_us) {}

    void onFrame(const CanFrame& frame) override {
        if (json_mode_) {
            std::cout << format_frame_json(frame, session_start_us_) << '\n';
        } else {
            std::cout << format_frame_text(frame, session_start_us_) << '\n';
        }
    }

    void onError(const TransportError& error) override {
        LOG_ERROR("Transport error: {}", error.what());
    }

private:
    bool json_mode_;
    uint64_t session_start_us_;
};

/// Generate a default output filename based on timestamp and format.
std::string default_output_path(const std::string& output_dir, LogFormat format) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);

    return std::format("{}/capture_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.{}",
                       output_dir,
                       tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                       tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                       log_format_extension(format));
}

} // anonymous namespace

int dispatch_record(CLI::App& sub, const GlobalOptions& globals, const Config& config) {
    LOG_DEBUG("dispatch_record: mock={}, json={}", globals.mock, globals.json);

    // --- Parse record-specific options ---
    uint32_t bitrate = config.bitrate;
    std::string output_path;
    std::string format_str;
    std::vector<std::string> filter_specs;

    if (auto* opt = sub.get_option_no_throw("--bitrate")) {
        if (opt->count() > 0) bitrate = opt->as<uint32_t>();
    }
    if (auto* opt = sub.get_option_no_throw("--output")) {
        if (opt->count() > 0) output_path = opt->as<std::string>();
    }
    if (auto* opt = sub.get_option_no_throw("--format")) {
        if (opt->count() > 0) format_str = opt->as<std::string>();
    }
    if (auto* opt = sub.get_option_no_throw("--filter")) {
        if (opt->count() > 0) filter_specs = opt->as<std::vector<std::string>>();
    }

    // --- Resolve format ---
    LogFormat format = config.output_format;
    if (!format_str.empty()) {
        if (!parse_log_format(format_str, format)) {
            std::cerr << "Error: Unknown format '" << format_str << "'. Use 'asc' or 'jsonl'.\n";
            return 1;
        }
    }

    // --- Resolve output path ---
    if (output_path.empty()) {
        output_path = default_output_path(config.output_directory, format);
    }

    // Ensure output directory exists
    std::filesystem::path out(output_path);
    if (out.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        if (ec) {
            std::cerr << "Error: Cannot create output directory: " << ec.message() << '\n';
            return 1;
        }
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

    // --- Select provider ---
    SessionService session;
    if (globals.mock) {
        LOG_INFO("Using MockProvider for record");
        session.setProvider(std::make_unique<MockProvider>());
    } else {
        LOG_INFO("Using J2534Provider for record");
        session.setProvider(std::make_unique<J2534Provider>());
    }

    // --- Discover and connect ---
    auto providers = session.scan();
    if (providers.empty()) {
        std::cerr << "Error: No J2534 providers found. Use --mock for simulated traffic.\n";
        return 2;
    }

    auto selected = select_provider(providers, config.provider);
    if (!selected) {
        std::cerr << "Error: No provider matching '" << config.provider << "'. Use 'canmatik scan' to list available providers.\n";
        return 2;
    }

    try {
        session.connect(*selected);
        session.openChannel(bitrate);
    } catch (const TransportError& e) {
        LOG_ERROR("Connection failed: {}", e.what());
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }

    // --- Set up capture service ---
    CaptureService capture;
    capture.setFilter(filter);

    uint64_t session_start_us = host_timestamp_us();

    RecordConsoleSink console_sink(globals.json, session_start_us);
    capture.addSink(&console_sink);

    // --- Set up record service ---
    RecordService recorder;
    capture.addSink(&recorder);

    // --- Print session header ---
    auto& status = session.mutableStatus();
    if (globals.mock) {
        status.provider_name = "MockProvider";
    }
    status.recording = true;
    status.recording_file = output_path;
    print_session_header(status, globals.json);

    // --- Start recording ---
    if (!recorder.start(output_path, format, status)) {
        std::cerr << "Error: Failed to open recording file: " << output_path << '\n';
        session.closeChannel();
        session.disconnect();
        return 1;
    }

    // --- Install Ctrl+C handler ---
    g_record_stop_requested.store(false);
    auto prev_handler = std::signal(SIGINT, record_signal_handler);

    // --- Start capture ---
    capture.start(session.channel(), status);

    // --- Main loop ---
    while (!g_record_stop_requested.load() && capture.isRunning()) {
        capture.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // --- Shutdown ---
    capture.stop();
    recorder.stop(status);

    std::signal(SIGINT, prev_handler);

    // Print recording summary
    print_recording_saved(output_path, recorder.frameCount(), globals.json);
    print_session_footer(status, globals.json);

    session.closeChannel();
    session.disconnect();

    return 0;
}

} // namespace canmatik
