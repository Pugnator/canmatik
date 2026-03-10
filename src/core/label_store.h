#pragma once

/// @file label_store.h
/// LabelStore: load/save/lookup of user-assigned arbitration ID labels.
/// Supports Constitution Principle VI (Incremental Reverse Engineering) and FR-021.

#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>

namespace canmatik {

/// Persistent store for user-assigned CAN arbitration ID labels.
/// Labels are stored as JSON in labels.json (working directory or config-relative).
class LabelStore {
public:
    /// Load labels from a JSON file.
    /// @param path Path to labels.json.
    /// @return Empty string on success, or error description.
    [[nodiscard]] std::string load(const std::string& path);

    /// Save labels to a JSON file.
    /// @param path Path to labels.json.
    /// @return Empty string on success, or error description.
    [[nodiscard]] std::string save(const std::string& path) const;

    /// Set or update a label for an arbitration ID.
    void set(uint32_t arb_id, const std::string& label);

    /// Remove a label for an arbitration ID.
    /// @return true if the label existed and was removed.
    bool remove(uint32_t arb_id);

    /// Look up a label for an arbitration ID.
    /// @return The label, or std::nullopt if not found.
    [[nodiscard]] std::optional<std::string> lookup(uint32_t arb_id) const;

    /// Get all labels.
    [[nodiscard]] const std::unordered_map<uint32_t, std::string>& labels() const {
        return labels_;
    }

    /// Check if the store is empty.
    [[nodiscard]] bool empty() const { return labels_.empty(); }

    /// Get the number of labels.
    [[nodiscard]] size_t size() const { return labels_.size(); }

private:
    std::unordered_map<uint32_t, std::string> labels_;
};

} // namespace canmatik
