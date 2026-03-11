#pragma once

/// @file frame_collector.h
/// Thread-safe frame storage with per-ID change tracking, ring buffer, and watchdog.

#include "core/can_frame.h"
#include "core/capture_sink.h"
#include "core/timestamp.h"
#include "gui/gui_state.h"

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace canmatik {

/// Per-ID state for change detection.
struct PerIdState {
    uint32_t arb_id        = 0;
    uint8_t  dlc           = 0;
    std::array<uint8_t, 8> data      = {};
    std::deque<std::array<uint8_t, 8>> history;     // last N payloads
    std::deque<uint8_t>                history_dlc;  // last N DLCs
    std::array<bool, 8>    changed   = {};
    bool     is_new        = true;
    bool     dlc_changed   = false;
    double   last_seen     = 0.0;
    uint64_t update_count  = 0;
};

/// Snapshot row for ImGui rendering.
struct MessageRow {
    uint32_t arb_id        = 0;
    uint8_t  dlc           = 0;
    std::array<uint8_t, 8> data     = {};
    std::array<bool, 8>    changed  = {};
    bool     is_new        = true;
    bool     dlc_changed   = false;
    double   last_seen     = 0.0;
    uint64_t update_count  = 0;
    bool     is_watched    = false;
};

/// Thread-safe frame collector: ring buffer + per-ID state + watchdog.
class FrameCollector : public ICaptureSync {
public:
    explicit FrameCollector(uint64_t session_start_us, uint32_t capacity = 100000);

    // ICaptureSync
    void onFrame(const CanFrame& frame) override;
    void onError(const TransportError& error) override;

    /// Push a frame (for replay controller bypass).
    void pushFrame(const CanFrame& frame);

    /// Take a filtered snapshot for rendering.
    std::vector<MessageRow> snapshot(bool changed_only, uint32_t change_n,
                                     ObdDisplayMode obd_mode,
                                     IdFilterMode id_filter_mode = IdFilterMode::EXCLUDE,
                                     const std::vector<uint32_t>& id_filter_list = {}) const;

    /// Take a watchdog-only snapshot.
    std::vector<MessageRow> watchdog_snapshot() const;

    // Watchdog management
    void add_watchdog(uint32_t id);
    void remove_watchdog(uint32_t id);
    void clear_watchdogs();
    bool is_watched(uint32_t id) const;

    // Buffer info
    uint64_t buffer_count() const;
    uint32_t buffer_capacity() const;
    void resize_buffer(uint32_t new_cap);

    /// Get all buffered frames in chronological order (for save-to-file).
    std::vector<CanFrame> buffer_contents() const;

    /// Reset all state.
    void clear();

    uint64_t unique_ids() const;

private:
    void ingest(const CanFrame& frame);
    static bool is_obd_id(uint32_t id);
    MessageRow to_row(const PerIdState& s) const;

    mutable std::mutex mu_;
    uint64_t session_start_us_;

    // Ring buffer
    std::vector<CanFrame> ring_;
    size_t ring_head_  = 0;
    size_t ring_count_ = 0;

    // Per-ID state
    std::unordered_map<uint32_t, PerIdState> ids_;

    // Watchdog
    std::unordered_set<uint32_t> watchdog_ids_;
};

} // namespace canmatik
