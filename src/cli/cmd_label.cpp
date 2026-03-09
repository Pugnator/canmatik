/// @file cmd_label.cpp
/// `canmatik label` subcommand — manage arbitration ID labels (T067 — Phase 10).

#include "cli/cli_app.h"
#include "core/label_store.h"
#include "core/log_macros.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace canmatik {

namespace {

constexpr const char* kDefaultLabelsPath = "labels.json";

/// Parse a hex string like "0x7E8" or "7E8" into a uint32_t.
/// Returns true on success.
bool parse_hex_id(const std::string& s, uint32_t& out) {
    std::string_view sv = s;
    if (sv.starts_with("0x") || sv.starts_with("0X")) {
        sv.remove_prefix(2);
    }
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), out, 16);
    return result.ec == std::errc{} && result.ptr == sv.data() + sv.size();
}

} // anonymous namespace

int dispatch_label(CLI::App& sub, const GlobalOptions& globals, const Config& /*config*/) {
    LOG_DEBUG("dispatch_label: json={}", globals.json);

    LabelStore store;
    auto load_err = store.load(kDefaultLabelsPath);
    if (!load_err.empty()) {
        LOG_WARNING("Labels file: {}", load_err);
    }

    for (auto* cmd : sub.get_subcommands()) {
        if (!cmd->parsed()) continue;
        const auto& name = cmd->get_name();

        if (name == "set") {
            auto id_str = cmd->get_option("id")->as<std::string>();
            auto label  = cmd->get_option("name")->as<std::string>();

            uint32_t arb_id = 0;
            if (!parse_hex_id(id_str, arb_id)) {
                std::cerr << std::format("Error: Invalid hex ID '{}'\n", id_str);
                return 1;
            }

            store.set(arb_id, label);
            auto err = store.save(kDefaultLabelsPath);
            if (!err.empty()) {
                std::cerr << std::format("Error: {}\n", err);
                return 1;
            }

            if (globals.json) {
                nlohmann::json j;
                j["action"] = "set";
                j["id"] = std::format("0x{:03X}", arb_id);
                j["label"] = label;
                std::cout << j.dump() << '\n';
            } else {
                std::cout << std::format("Label set: 0x{:03X} = {}\n", arb_id, label);
            }
            return 0;
        }

        if (name == "remove") {
            auto id_str = cmd->get_option("id")->as<std::string>();

            uint32_t arb_id = 0;
            if (!parse_hex_id(id_str, arb_id)) {
                std::cerr << std::format("Error: Invalid hex ID '{}'\n", id_str);
                return 1;
            }

            if (!store.remove(arb_id)) {
                std::cerr << std::format("No label found for 0x{:03X}\n", arb_id);
                return 1;
            }

            auto err = store.save(kDefaultLabelsPath);
            if (!err.empty()) {
                std::cerr << std::format("Error: {}\n", err);
                return 1;
            }

            if (globals.json) {
                nlohmann::json j;
                j["action"] = "removed";
                j["id"] = std::format("0x{:03X}", arb_id);
                std::cout << j.dump() << '\n';
            } else {
                std::cout << std::format("Label removed: 0x{:03X}\n", arb_id);
            }
            return 0;
        }

        if (name == "list") {
            if (globals.json) {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& [id, lbl] : store.labels()) {
                    nlohmann::json entry;
                    entry["id"] = std::format("0x{:03X}", id);
                    entry["label"] = lbl;
                    arr.push_back(std::move(entry));
                }
                std::cout << arr.dump(2) << '\n';
            } else {
                if (store.empty()) {
                    std::cout << "No labels defined. Use 'canmatik label set <id> <name>' to add one.\n";
                } else {
                    std::cout << std::format("{} label(s):\n", store.size());
                    for (const auto& [id, lbl] : store.labels()) {
                        std::cout << std::format("  0x{:03X}  {}\n", id, lbl);
                    }
                }
            }
            return 0;
        }
    }

    std::cerr << "Error: No label subcommand selected. Use --help for usage.\n";
    return 1;
}

} // namespace canmatik
