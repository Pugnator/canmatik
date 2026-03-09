/// @file filter.cpp
/// FilterEngine implementation and filter spec parser.

#include "core/filter.h"
#include "core/can_frame.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace canmatik {

namespace {

/// Parse a hex string like "0x7E8" or "7E8" into a uint32_t.
bool parse_hex(const std::string& s, uint32_t& out) {
    std::string_view sv = s;
    if (sv.starts_with("0x") || sv.starts_with("0X")) {
        sv.remove_prefix(2);
    }
    if (sv.empty()) return false;
    auto result = std::from_chars(sv.data(), sv.data() + sv.size(), out, 16);
    return result.ec == std::errc{} && result.ptr == sv.data() + sv.size();
}

} // anonymous namespace

std::string parse_filter(const std::string& spec, FilterRule& out) {
    if (spec.empty()) {
        return "Empty filter specification";
    }

    std::string s = spec;
    out = FilterRule{};

    // Check for action prefix
    if (s.starts_with("pass:")) {
        out.action = FilterAction::Pass;
        s = s.substr(5);
    } else if (s.starts_with("block:")) {
        out.action = FilterAction::Block;
        s = s.substr(6);
    } else if (s.starts_with("!")) {
        out.action = FilterAction::Block;
        s = s.substr(1);
    } else {
        out.action = FilterAction::Pass;
    }

    // Check for range syntax: 0x7E0-0x7EF
    if (auto dash = s.find('-'); dash != std::string::npos && !s.starts_with("-")) {
        uint32_t lo, hi;
        if (!parse_hex(s.substr(0, dash), lo)) {
            return std::format("Invalid range start in '{}'", spec);
        }
        if (!parse_hex(s.substr(dash + 1), hi)) {
            return std::format("Invalid range end in '{}'", spec);
        }
        if (lo > hi) {
            return std::format("Range start 0x{:X} > end 0x{:X} in '{}'", lo, hi, spec);
        }
        // Compute a mask that covers the range [lo, hi].
        // For simple power-of-two aligned ranges, this is exact.
        // For arbitrary ranges, we compute the common prefix mask.
        uint32_t diff = lo ^ hi;
        uint32_t mask = 0xFFFFFFFF;
        while (mask & diff) {
            mask <<= 1;
        }
        out.id_value = lo & mask;
        out.id_mask = mask;
        return {};
    }

    // Check for mask syntax: 0x700/0xFF0
    if (auto slash = s.find('/'); slash != std::string::npos) {
        if (!parse_hex(s.substr(0, slash), out.id_value)) {
            return std::format("Invalid ID in '{}'", spec);
        }
        if (!parse_hex(s.substr(slash + 1), out.id_mask)) {
            return std::format("Invalid mask in '{}'", spec);
        }
        return {};
    }

    // Simple ID match: 0x7E8
    if (!parse_hex(s, out.id_value)) {
        return std::format("Invalid filter specification '{}'", spec);
    }
    out.id_mask = 0xFFFFFFFF; // exact match
    return {};
}

void FilterEngine::add_rule(const FilterRule& rule) {
    rules_.push_back(rule);
    if (rule.action == FilterAction::Pass) {
        has_pass_rules_ = true;
    }
}

void FilterEngine::clear() {
    rules_.clear();
    has_pass_rules_ = false;
}

bool FilterEngine::evaluate(uint32_t arbitration_id) const {
    if (rules_.empty()) {
        return true; // No filters = pass all
    }

    // Check block rules first — always exclude
    for (const auto& rule : rules_) {
        if (rule.action == FilterAction::Block) {
            if ((arbitration_id & rule.id_mask) == (rule.id_value & rule.id_mask)) {
                return false; // Blocked
            }
        }
    }

    // If pass rules exist, frame must match at least one
    if (has_pass_rules_) {
        for (const auto& rule : rules_) {
            if (rule.action == FilterAction::Pass) {
                if ((arbitration_id & rule.id_mask) == (rule.id_value & rule.id_mask)) {
                    return true; // Passed
                }
            }
        }
        return false; // No pass rule matched
    }

    // Only block rules, and none matched — pass
    return true;
}

} // namespace canmatik
