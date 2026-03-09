/// @file cmd_monitor.cpp
/// `canmatik monitor` subcommand — connect and display CAN frames in real time (T033 — US2).

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
#include "services/session_service.h"
#include "transport/transport_error.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace canmatik {

namespace {

/// Global flag for Ctrl+C handling — shared across signal handler and main loop.
std::atomic<bool> g_stop_requested{false};

void signal_handler(int /*sig*/) {
    g_stop_requested.store(true);
}

/// Sink that prints frames to stdout.
class ConsoleSink : public ICaptureSync {
public:
    ConsoleSink(bool json_mode, uint64_t session_start_us)
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

} // anonymous namespace

int dispatch_monitor(CLI::App& sub, const GlobalOptions& globals, const Config& config) {
    LOG_DEBUG("dispatch_monitor: mock={}, json={}, verbose={}",
              globals.mock, globals.json, globals.verbose);

    // --- Parse monitor-specific options ---
    uint32_t bitrate = 500000;
    std::vector<std::string> filter_specs;

    if (auto* opt = sub.get_option_no_throw("--bitrate")) {
        if (opt->count() > 0) bitrate = opt->as<uint32_t>();
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

    // --- Select provider ---
    SessionService session;
    if (globals.mock) {
        LOG_INFO("Using MockProvider for monitor");
        session.setProvider(std::make_unique<MockProvider>());
    } else {
        LOG_INFO("Using J2534Provider for monitor");
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
    ConsoleSink console_sink(globals.json, session_start_us);
    capture.addSink(&console_sink);

    // --- Print session header ---
    auto& status = session.mutableStatus();
    if (globals.mock) {
        status.provider_name = "MockProvider";
    }
    print_session_header(status, globals.json);

    // --- Install Ctrl+C handler ---
    g_stop_requested.store(false);
    auto prev_handler = std::signal(SIGINT, signal_handler);

    // --- Start capture ---
    capture.start(session.channel(), status);

    // --- Main loop: drain queue and check for stop ---
    while (!g_stop_requested.load() && capture.isRunning()) {
        capture.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // --- Shutdown ---
    capture.stop();

    // Restore signal handler
    std::signal(SIGINT, prev_handler);

    // Print session summary
    print_session_footer(status, globals.json);

    session.closeChannel();
    session.disconnect();

    return 0;
}

} // namespace canmatik
