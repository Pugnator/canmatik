/// @file cmd_demo.cpp
/// `canmatik demo` subcommand — run with mock backend for simulated traffic (T057 — US7).

#include "cli/cli_app.h"
#include "cli/formatters.h"
#include "cli/provider_select.h"
#include "core/capture_sink.h"
#include "core/filter.h"
#include "core/log_macros.h"
#include "core/timestamp.h"
#include "logging/asc_reader.h"
#include "logging/jsonl_reader.h"
#include "mock/mock_channel.h"
#include "mock/mock_provider.h"
#include "services/capture_service.h"
#include "services/session_service.h"
#include "transport/transport_error.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace canmatik {

namespace {

std::atomic<bool> g_demo_stop_requested{false};

void demo_signal_handler(int /*sig*/) {
    g_demo_stop_requested.store(true);
}

/// Sink that prints frames to stdout with [MOCK] awareness.
class DemoConsoleSink : public ICaptureSync {
public:
    DemoConsoleSink(bool json_mode, uint64_t session_start_us)
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

/// Load frames from a trace file for replay via MockChannel.
std::vector<CanFrame> load_trace_frames(const std::string& trace_path) {
    auto ext = std::filesystem::path(trace_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::unique_ptr<ILogReader> reader;
    if (ext == ".asc") {
        reader = std::make_unique<AscReader>();
    } else if (ext == ".jsonl") {
        reader = std::make_unique<JsonlReader>();
    } else {
        LOG_ERROR("Unsupported trace file format: {}", ext);
        return {};
    }

    if (!reader->open(trace_path)) {
        LOG_ERROR("Failed to open trace file: {}", trace_path);
        return {};
    }

    std::vector<CanFrame> frames;
    while (auto f = reader->nextFrame()) {
        frames.push_back(*f);
    }
    return frames;
}

} // anonymous namespace

int dispatch_demo(CLI::App& sub, const GlobalOptions& globals, const Config& config) {
    LOG_DEBUG("dispatch_demo: json={}", globals.json);

    // --- Parse demo-specific options ---
    uint32_t bitrate = config.bitrate;
    uint32_t frame_rate = config.mock_frame_rate;
    std::string trace_path;
    std::vector<std::string> filter_specs;

    if (auto* opt = sub.get_option_no_throw("--bitrate")) {
        if (opt->count() > 0) bitrate = opt->as<uint32_t>();
    }
    if (auto* opt = sub.get_option_no_throw("--frame-rate")) {
        if (opt->count() > 0) frame_rate = opt->as<uint32_t>();
    }
    if (auto* opt = sub.get_option_no_throw("--trace")) {
        if (opt->count() > 0) trace_path = opt->as<std::string>();
    }
    if (auto* opt = sub.get_option_no_throw("--filter")) {
        if (opt->count() > 0) filter_specs = opt->as<std::vector<std::string>>();
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

    // --- Setup mock provider ---
    SessionService session;
    session.setProvider(std::make_unique<MockProvider>());

    auto providers = session.scan();
    if (providers.empty()) {
        std::cerr << "Error: MockProvider returned no devices.\n";
        return 2;
    }

    auto selected = select_provider(providers, config.provider);
    if (!selected) {
        std::cerr << "Error: No provider matching '" << config.provider << "'.\n";
        return 2;
    }

    try {
        session.connect(*selected);
        session.openChannel(bitrate);
    } catch (const TransportError& e) {
        LOG_ERROR("Mock connection failed: {}", e.what());
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }

    // --- Load trace file if specified ---
    if (!trace_path.empty()) {
        auto trace_frames = load_trace_frames(trace_path);
        if (trace_frames.empty()) {
            std::cerr << "Error: No frames in trace file: " << trace_path << '\n';
            session.closeChannel();
            session.disconnect();
            return 1;
        }
        // MockChannel supports set_frame_sequence via dynamic_cast
        auto* mock_ch = dynamic_cast<MockChannel*>(session.channel());
        if (mock_ch) {
            mock_ch->set_frame_sequence(std::move(trace_frames));
        }
        LOG_INFO("Loaded trace file: {} ({} frames)", trace_path, trace_frames.size());
    }

    // --- Set up capture service ---
    CaptureService capture;
    capture.setFilter(filter);

    uint64_t session_start_us = host_timestamp_us();
    DemoConsoleSink console_sink(globals.json, session_start_us);
    capture.addSink(&console_sink);

    // --- Print session header ---
    auto& status = session.mutableStatus();
    status.provider_name = "MockProvider";
    print_session_header(status, globals.json);

    // --- Install Ctrl+C handler ---
    g_demo_stop_requested.store(false);
    auto prev_handler = std::signal(SIGINT, demo_signal_handler);

    // --- Start capture ---
    capture.start(session.channel(), status);

    // --- Main loop ---
    while (!g_demo_stop_requested.load() && capture.isRunning()) {
        capture.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // --- Shutdown ---
    capture.stop();
    std::signal(SIGINT, prev_handler);
    print_session_footer(status, globals.json);

    session.closeChannel();
    session.disconnect();

    return 0;
}

} // namespace canmatik
