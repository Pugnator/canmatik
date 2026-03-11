#pragma once

/// @file sniff_panel.h
/// ImGui panel: live CAN frame sniff view with byte-level diff highlighting.

#include "core/can_frame.h"
#include "core/capture_sink.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace canmatik {

/// A single row of display data for one arbitration ID.
struct SniffRow {
    uint32_t arb_id        = 0;
    uint8_t  dlc           = 0;
    std::array<uint8_t, 8> data     = {};
    std::array<bool,    8> changed  = {};  ///< true for each byte that changed
    bool     is_new        = true;         ///< first time seen
    bool     dlc_changed   = false;
    double   last_seen     = 0.0;          ///< relative timestamp (seconds)
    uint64_t update_count  = 0;
};

/// ICaptureSync implementation that collects sniff rows for ImGui rendering.
/// Thread-safe: the reader thread calls onFrame, the GUI thread calls snapshot().
class SniffCollector : public ICaptureSync {
public:
    explicit SniffCollector(uint64_t session_start_us);

    void onFrame(const CanFrame& frame) override;
    void onError(const TransportError& error) override;

    /// Take a copy of current rows sorted by arb_id for rendering.
    std::vector<SniffRow> snapshot();

    uint64_t unique_ids() const;

private:
    uint64_t session_start_us_;
    mutable std::mutex mu_;
    std::unordered_map<uint32_t, SniffRow> rows_;
};

/// Render the sniff panel using ImGui.  Call from the main render loop.
void render_sniff_panel(SniffCollector& collector);

} // namespace canmatik
