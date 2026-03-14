#pragma once

/// @file scriptable_mock_channel.h
/// ScriptableMockChannel : IChannel — YAML-driven request→response ECU simulator.

#include "transport/channel.h"
#include "core/can_frame.h"

#include <vector>
#include <deque>
#include <mutex>
#include <string>
#include <cstdint>

namespace canmatik {

/// A single rule: if a written frame matches `request`, queue `responses`.
struct MockRule {
    uint32_t request_id = 0;
    std::vector<uint8_t> request_data;   ///< prefix match (1..8 bytes)
    std::vector<CanFrame> responses;     ///< frames returned on next read()
};

/// Load a rule set from a YAML file.  Returns empty vector on parse error.
std::vector<MockRule> load_mock_rules(const std::string& yaml_path,
                                      std::string* out_error = nullptr);

/// Mock CAN channel that responds to writes according to a rule table.
/// Thread-safe: write() may be called from one thread while read() blocks
/// on another (typical OBD session pattern).
class ScriptableMockChannel : public IChannel {
public:
    explicit ScriptableMockChannel(std::vector<MockRule> rules);

    void open(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN) override;
    void close() override;
    std::vector<CanFrame> read(uint32_t timeout_ms) override;
    void write(const CanFrame& frame) override;
    void setFilter(uint32_t mask, uint32_t pattern) override;
    void clearFilters() override;
    [[nodiscard]] bool isOpen() const override;

    /// Read-only access to the rule table (for diagnostics / logging).
    [[nodiscard]] const std::vector<MockRule>& rules() const { return rules_; }

private:
    std::vector<MockRule> rules_;
    std::deque<CanFrame>  pending_;     ///< response queue
    mutable std::mutex    mu_;
    bool open_ = false;
};

} // namespace canmatik
