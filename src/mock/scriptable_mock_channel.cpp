#include "mock/scriptable_mock_channel.h"

#include "core/log_macros.h"
#include "core/timestamp.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace canmatik {

// ---------------------------------------------------------------------------
// YAML loader
// ---------------------------------------------------------------------------

/// Parse a hex string like "0x7DF" or "7DF" into uint32_t.
static uint32_t parse_hex_id(const YAML::Node& node) {
    if (node.IsScalar()) {
        auto s = node.as<std::string>();
        return static_cast<uint32_t>(std::stoul(s, nullptr, 0));
    }
    return node.as<uint32_t>();
}

/// Parse a data array: accepts [0x02, 0x01, 0x00] or ["02","01","00"].
static std::vector<uint8_t> parse_data(const YAML::Node& node) {
    std::vector<uint8_t> out;
    if (!node.IsSequence()) return out;
    for (const auto& v : node) {
        if (v.IsScalar()) {
            auto s = v.as<std::string>();
            out.push_back(static_cast<uint8_t>(std::stoul(s, nullptr, 0)));
        } else {
            out.push_back(v.as<uint8_t>());
        }
    }
    return out;
}

/// Build a CanFrame from a YAML map {id: 0x7E8, data: [...]}.
static CanFrame frame_from_yaml(const YAML::Node& node) {
    CanFrame f{};
    f.type = FrameType::Standard;
    if (node["id"]) f.arbitration_id = parse_hex_id(node["id"]);
    if (node["data"]) {
        auto bytes = parse_data(node["data"]);
        f.dlc = static_cast<uint8_t>(std::min<size_t>(bytes.size(), 8));
        f.data.fill(0x55); // ISO-TP padding
        for (size_t i = 0; i < f.dlc; ++i) f.data[i] = bytes[i];
    }
    return f;
}

std::vector<MockRule> load_mock_rules(const std::string& yaml_path,
                                      std::string* out_error) {
    std::vector<MockRule> rules;
    try {
        YAML::Node root = YAML::LoadFile(yaml_path);

        auto responses_node = root["responses"];
        if (!responses_node || !responses_node.IsSequence()) {
            if (out_error) *out_error = "YAML missing 'responses' sequence";
            return rules;
        }

        for (const auto& entry : responses_node) {
            MockRule rule;

            // Parse request
            auto req = entry["request"];
            if (!req) continue;
            rule.request_id = req["id"] ? parse_hex_id(req["id"]) : 0;
            rule.request_data = req["data"] ? parse_data(req["data"]) : std::vector<uint8_t>{};

            // Parse response — single or list
            if (entry["response"]) {
                auto resp_node = entry["response"];
                if (resp_node.IsSequence() && resp_node.size() > 0 && resp_node[0].IsMap() && resp_node[0]["id"]) {
                    // Array of frames
                    for (const auto& rf : resp_node) {
                        rule.responses.push_back(frame_from_yaml(rf));
                    }
                } else {
                    // Single frame
                    rule.responses.push_back(frame_from_yaml(resp_node));
                }
            }

            if (!rule.responses.empty()) {
                LOG_DEBUG("Mock rule: id=0x{:X} data[{}] -> {} response frame(s)",
                          rule.request_id, rule.request_data.size(), rule.responses.size());
                rules.push_back(std::move(rule));
            }
        }

        LOG_INFO("Loaded {} mock rules from {}", rules.size(), yaml_path);
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to load mock rules from '{}': {}", yaml_path, ex.what());
        if (out_error) *out_error = ex.what();
    }
    return rules;
}

// ---------------------------------------------------------------------------
// ScriptableMockChannel
// ---------------------------------------------------------------------------

ScriptableMockChannel::ScriptableMockChannel(std::vector<MockRule> rules)
    : rules_(std::move(rules)) {}

void ScriptableMockChannel::open(uint32_t /*bitrate*/, BusProtocol /*protocol*/) {
    open_ = true;
}

void ScriptableMockChannel::close() {
    open_ = false;
    std::lock_guard<std::mutex> lk(mu_);
    pending_.clear();
}

void ScriptableMockChannel::write(const CanFrame& frame) {
    if (!open_) return;

    // Match against rules: check ID and data prefix
    for (const auto& rule : rules_) {
        // ID match: 0 = wildcard (match any), otherwise exact
        if (rule.request_id != 0 && rule.request_id != frame.arbitration_id)
            continue;

        // Data prefix match
        bool data_match = true;
        for (size_t i = 0; i < rule.request_data.size(); ++i) {
            if (i >= frame.dlc || rule.request_data[i] != frame.data[i]) {
                data_match = false;
                break;
            }
        }
        if (!data_match) continue;

        // Match found — queue response frames
        std::lock_guard<std::mutex> lk(mu_);
        for (auto resp : rule.responses) {
            resp.host_timestamp_us = host_timestamp_us();
            pending_.push_back(resp);
        }
        LOG_DEBUG("Mock matched rule: id=0x{:X} -> {} response(s)",
                  frame.arbitration_id, rule.responses.size());
        return;  // first matching rule wins
    }

    LOG_DEBUG("Mock: no rule for id=0x{:X} dlc={}", frame.arbitration_id, frame.dlc);
}

std::vector<CanFrame> ScriptableMockChannel::read(uint32_t timeout_ms) {
    std::vector<CanFrame> out;
    if (!open_) return out;

    // Fast path: frames already queued
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!pending_.empty()) {
            out.assign(pending_.begin(), pending_.end());
            pending_.clear();
            return out;
        }
    }

    // Wait up to timeout_ms for a response to be queued by write()
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard<std::mutex> lk(mu_);
        if (!pending_.empty()) {
            out.assign(pending_.begin(), pending_.end());
            pending_.clear();
            return out;
        }
    }

    return out; // timeout — empty
}

void ScriptableMockChannel::setFilter(uint32_t /*mask*/, uint32_t /*pattern*/) {}
void ScriptableMockChannel::clearFilters() {}
bool ScriptableMockChannel::isOpen() const { return open_; }

} // namespace canmatik
