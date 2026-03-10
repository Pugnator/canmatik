/// @file cmd_obd.cpp
/// `canmatik obd` subcommand — OBD-II diagnostics (query, stream, dtc, info).

#include "cli/cli_app.h"
#include "cli/formatters.h"
#include "cli/provider_select.h"
#include "core/log_macros.h"
#include "core/timestamp.h"
#include "mock/mock_provider.h"
#include "obd/interval_spec.h"
#include "obd/obd_config.h"
#include "obd/obd_session.h"
#include "obd/pid_table.h"
#include "obd/query_scheduler.h"
#include "platform/win32/j2534_provider.h"
#include "services/capture_service.h"
#include "services/record_service.h"
#include "services/session_service.h"
#include "transport/transport_error.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <thread>

namespace canmatik {

namespace {

std::atomic<bool> g_obd_stop{false};

void obd_signal_handler(int /*sig*/) {
    g_obd_stop.store(true);
}

/// Connect to a J2534 provider, returning configured session service.
/// On error, prints message and returns nullptr.
std::unique_ptr<SessionService> connect_obd(const GlobalOptions& globals, const Config& config,
                                             uint32_t bitrate) {
    auto session = std::make_unique<SessionService>();
    if (globals.mock) {
        LOG_INFO("Using MockProvider for OBD");
        session->setProvider(std::make_unique<MockProvider>());
    } else {
        session->setProvider(std::make_unique<J2534Provider>());
    }

    auto providers = session->scan();
    if (providers.empty()) {
        std::cerr << "Error: No J2534 providers found. Use --mock for simulated traffic.\n";
        return nullptr;
    }

    auto selected = select_provider(providers, config.provider);
    if (!selected) {
        std::cerr << "Error: No provider matching '" << config.provider << "'.\n";
        return nullptr;
    }

    try {
        session->connect(*selected);
        session->openChannel(bitrate);
    } catch (const TransportError& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return nullptr;
    }

    return session;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// dispatch_obd — top-level router for obd sub-subcommands
// ---------------------------------------------------------------------------
int dispatch_obd(CLI::App& sub, const GlobalOptions& globals, const Config& config) {
    LOG_DEBUG("dispatch_obd");

    uint32_t bitrate = 500000;
    if (auto* opt = sub.get_option_no_throw("--bitrate")) {
        if (opt->count() > 0) bitrate = opt->as<uint32_t>();
    }

    for (auto* child : sub.get_subcommands()) {
        if (!child->parsed()) continue;
        const auto& name = child->get_name();

        // ---- obd query --supported ----
        if (name == "query") {
            auto session = connect_obd(globals, config, bitrate);
            if (!session) return 2;

            ObdSession obd(*session->channel());
            auto result = obd.query_supported_pids();
            if (!result) {
                std::cerr << "Error: " << result.error() << '\n';
                return 1;
            }

            if (globals.json) {
                nlohmann::json j = nlohmann::json::array();
                for (const auto& sp : *result) {
                    nlohmann::json entry;
                    entry["ecu_id"] = std::format("0x{:03X}", sp.ecu_id);
                    entry["pids"] = nlohmann::json::array();
                    for (auto p : sp.pids) {
                        const auto* def = pid_lookup(0x01, p);
                        nlohmann::json pj;
                        pj["pid"] = std::format("0x{:02X}", p);
                        if (def) pj["name"] = def->name;
                        entry["pids"].push_back(pj);
                    }
                    j.push_back(entry);
                }
                std::cout << j.dump(2) << '\n';
            } else {
                for (const auto& sp : *result) {
                    std::cout << std::format("ECU 0x{:03X}:\n", sp.ecu_id);
                    for (auto p : sp.pids) {
                        const auto* def = pid_lookup(0x01, p);
                        if (def) {
                            std::cout << std::format("  0x{:02X}  {}\n", p, def->name);
                        } else {
                            std::cout << std::format("  0x{:02X}\n", p);
                        }
                    }
                }
            }

            session->closeChannel();
            session->disconnect();
            return 0;
        }

        // ---- obd stream ----
        if (name == "stream") {
            std::string obd_config_path;
            std::string interval_override;
            if (auto* opt = child->get_option_no_throw("--obd-config")) {
                if (opt->count() > 0) obd_config_path = opt->as<std::string>();
            }
            if (auto* opt = child->get_option_no_throw("--interval")) {
                if (opt->count() > 0) interval_override = opt->as<std::string>();
            }

            // Load or generate OBD config
            ObdConfig obd_cfg;
            if (!obd_config_path.empty()) {
                auto loaded = ObdConfig::load(obd_config_path);
                if (!loaded) {
                    std::cerr << "Error: " << loaded.error() << '\n';
                    return 1;
                }
                obd_cfg = *loaded;
            } else if (std::filesystem::exists("obd.yaml")) {
                auto loaded = ObdConfig::load("obd.yaml");
                if (!loaded) {
                    std::cerr << "Error: " << loaded.error() << '\n';
                    return 1;
                }
                obd_cfg = *loaded;
            } else {
                auto gen = ObdConfig::generate_default("obd.yaml");
                if (!gen) {
                    std::cerr << "Warning: " << gen.error() << '\n';
                }
                LOG_INFO("Generated default obd.yaml");
                auto loaded = ObdConfig::load("obd.yaml");
                if (!loaded) {
                    std::cerr << "Error: " << loaded.error() << '\n';
                    return 1;
                }
                obd_cfg = *loaded;
            }

            // Apply interval override
            if (!interval_override.empty()) {
                auto iv = parse_interval(interval_override);
                if (!iv) {
                    std::cerr << "Error: --interval: " << iv.error() << '\n';
                    return 1;
                }
                obd_cfg.default_interval = *iv;
                for (auto& g : obd_cfg.groups) {
                    g.interval = *iv;
                    g.has_interval = true;
                }
            }

            auto session = connect_obd(globals, config, bitrate);
            if (!session) return 2;

            ObdSession obd(*session->channel(), obd_cfg.tx_id, obd_cfg.rx_base);

            g_obd_stop.store(false);
            auto prev = std::signal(SIGINT, obd_signal_handler);

            QueryScheduler scheduler(obd, obd_cfg, [&](const DecodedPid& decoded) {
                if (globals.json) {
                    nlohmann::json j;
                    j["ecu_id"] = std::format("0x{:03X}", decoded.ecu_id);
                    j["pid"] = std::format("0x{:02X}", decoded.pid);
                    j["name"] = decoded.name;
                    j["value"] = decoded.value;
                    j["unit"] = decoded.unit;
                    std::cout << j.dump() << '\n';
                } else {
                    std::cout << std::format("  0x{:02X}  {:>8.1f} {:5s}  {}\n",
                                             decoded.pid, decoded.value, decoded.unit,
                                             decoded.name);
                }
            });

            scheduler.run(g_obd_stop);

            std::signal(SIGINT, prev);
            session->closeChannel();
            session->disconnect();
            return 0;
        }

        // ---- obd dtc ----
        if (name == "dtc") {
            bool clear_flag = false;
            bool force_flag = false;
            if (auto* opt = child->get_option_no_throw("--clear")) {
                clear_flag = opt->count() > 0;
            }
            if (auto* opt = child->get_option_no_throw("--force")) {
                force_flag = opt->count() > 0;
            }

            auto session = connect_obd(globals, config, bitrate);
            if (!session) return 2;

            ObdSession obd(*session->channel());

            if (clear_flag) {
                auto result = obd.clear_dtcs(force_flag);
                if (!result) {
                    std::cerr << "Error: " << result.error() << '\n';
                    session->closeChannel();
                    session->disconnect();
                    return 1;
                }
                std::cout << "DTCs cleared successfully.\n";
            } else {
                // Read stored DTCs
                auto stored = obd.read_dtcs();
                auto pending = obd.read_pending_dtcs();

                if (globals.json) {
                    nlohmann::json j;
                    j["stored"] = nlohmann::json::array();
                    if (stored) {
                        for (const auto& d : *stored) {
                            j["stored"].push_back({
                                {"code", d.code},
                                {"category", static_cast<int>(d.category)},
                                {"ecu_id", std::format("0x{:03X}", d.ecu_id)}
                            });
                        }
                    }
                    j["pending"] = nlohmann::json::array();
                    if (pending) {
                        for (const auto& d : *pending) {
                            j["pending"].push_back({
                                {"code", d.code},
                                {"category", static_cast<int>(d.category)},
                                {"ecu_id", std::format("0x{:03X}", d.ecu_id)}
                            });
                        }
                    }
                    std::cout << j.dump(2) << '\n';
                } else {
                    if (stored && !stored->empty()) {
                        std::cout << "Stored DTCs:\n";
                        for (const auto& d : *stored) {
                            std::cout << std::format("  {}  (ECU 0x{:03X})\n",
                                                     d.code, d.ecu_id);
                        }
                    } else {
                        std::cout << "No stored DTCs.\n";
                    }
                    if (pending && !pending->empty()) {
                        std::cout << "Pending DTCs:\n";
                        for (const auto& d : *pending) {
                            std::cout << std::format("  {}  (ECU 0x{:03X})\n",
                                                     d.code, d.ecu_id);
                        }
                    } else {
                        std::cout << "No pending DTCs.\n";
                    }
                }
            }

            session->closeChannel();
            session->disconnect();
            return 0;
        }

        // ---- obd info ----
        if (name == "info") {
            auto session = connect_obd(globals, config, bitrate);
            if (!session) return 2;

            ObdSession obd(*session->channel());
            auto result = obd.read_vehicle_info();

            if (!result) {
                std::cerr << "Error: " << result.error() << '\n';
                session->closeChannel();
                session->disconnect();
                return 1;
            }

            if (globals.json) {
                nlohmann::json j;
                j["vin"] = result->vin;
                j["calibration_id"] = result->calibration_id;
                j["ecu_name"] = result->ecu_name;
                std::cout << j.dump(2) << '\n';
            } else {
                std::cout << "Vehicle Information:\n";
                if (!result->vin.empty())
                    std::cout << "  VIN:            " << result->vin << '\n';
                if (!result->calibration_id.empty())
                    std::cout << "  Calibration ID: " << result->calibration_id << '\n';
                if (!result->ecu_name.empty())
                    std::cout << "  ECU Name:       " << result->ecu_name << '\n';
            }

            session->closeChannel();
            session->disconnect();
            return 0;
        }
    }

    std::cerr << "Error: No OBD subcommand selected. Run 'canmatik obd --help'.\n";
    return 1;
}

} // namespace canmatik
