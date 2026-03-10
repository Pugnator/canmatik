/// @file label_store.cpp
/// LabelStore JSON persistence implementation.

#include "core/label_store.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <format>
#include <chrono>

namespace canmatik {

std::string LabelStore::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Not an error — file may not exist yet (first run)
        return {};
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return std::format("Labels JSON parse error: {}", e.what());
    }

    if (!j.is_array()) {
        return "Labels file must contain a JSON array";
    }

    labels_.clear();
    for (const auto& entry : j) {
        if (!entry.contains("arbitration_id") || !entry.contains("label")) {
            continue; // skip malformed entries
        }

        uint32_t arb_id = 0;
        const auto& id_val = entry["arbitration_id"];
        if (id_val.is_string()) {
            // Parse hex string like "0x7E8"
            std::string s = id_val.get<std::string>();
            std::string_view sv = s;
            if (sv.starts_with("0x") || sv.starts_with("0X")) {
                sv.remove_prefix(2);
            }
            auto result = std::from_chars(sv.data(), sv.data() + sv.size(), arb_id, 16);
            if (result.ec != std::errc{}) continue;
        } else if (id_val.is_number_unsigned()) {
            arb_id = id_val.get<uint32_t>();
        } else {
            continue;
        }

        std::string label = entry["label"].get<std::string>();
        if (!label.empty()) {
            labels_[arb_id] = std::move(label);
        }
    }

    return {};
}

std::string LabelStore::save(const std::string& path) const {
    nlohmann::json j = nlohmann::json::array();

    for (const auto& [arb_id, label] : labels_) {
        nlohmann::json entry;
        entry["arbitration_id"] = std::format("0x{:03X}", arb_id);
        entry["label"] = label;
        // created_utc: use current time for simplicity
        entry["created_utc"] = "";
        j.push_back(std::move(entry));
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        return std::format("Cannot write labels file: {}", path);
    }

    file << j.dump(2) << '\n';
    return {};
}

void LabelStore::set(uint32_t arb_id, const std::string& label) {
    labels_[arb_id] = label;
}

bool LabelStore::remove(uint32_t arb_id) {
    return labels_.erase(arb_id) > 0;
}

std::optional<std::string> LabelStore::lookup(uint32_t arb_id) const {
    auto it = labels_.find(arb_id);
    if (it != labels_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace canmatik
