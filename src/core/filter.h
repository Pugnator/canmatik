#pragma once

/// @file filter.h
/// FilterRule, FilterAction, and FilterEngine for ID-based frame filtering.

#include <cstdint>
#include <vector>
#include <string>

namespace canmatik {

struct CanFrame; // forward declaration

/// Whether a matching frame is included or excluded.
enum class FilterAction {
    Pass,   ///< Frame is included in output
    Block,  ///< Frame is excluded from output
};

/// A single filter criterion. Multiple rules combine into a filter set.
struct FilterRule {
    FilterAction action = FilterAction::Pass;
    uint32_t id_value   = 0;          ///< Arbitration ID to match
    uint32_t id_mask    = 0xFFFFFFFF; ///< Bitmask: (frame.id & mask) == (id_value & mask)
};

/// Parse a filter specification string into a FilterRule.
/// Supported syntaxes: "0x7E8", "0x7E0-0x7EF", "0x700/0xFF0",
///                     "!0x000", "pass:0x7E8", "block:0x000"
/// Returns empty string on success, or error description.
[[nodiscard]] std::string parse_filter(const std::string& spec, FilterRule& out);

/// Engine that evaluates a set of filter rules against frames.
///
/// Combination logic:
/// - No filters = pass all (default).
/// - If any Pass rule is present, only frames matching at least one Pass rule
///   are shown (whitelist mode).
/// - Block rules always exclude, regardless of Pass rules.
class FilterEngine {
public:
    /// Add a filter rule to the engine.
    void add_rule(const FilterRule& rule);

    /// Remove all rules.
    void clear();

    /// Evaluate whether a frame should be displayed/recorded.
    /// @return true if the frame passes the filter (should be shown).
    [[nodiscard]] bool evaluate(uint32_t arbitration_id) const;

    /// Get the current rule set (for status display).
    [[nodiscard]] const std::vector<FilterRule>& rules() const { return rules_; }

    /// Check if any rules are active.
    [[nodiscard]] bool empty() const { return rules_.empty(); }

private:
    std::vector<FilterRule> rules_;
    bool has_pass_rules_ = false;  ///< Cache: true if any Pass rule exists
};

} // namespace canmatik
