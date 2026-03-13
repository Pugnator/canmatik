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

/// How to decode a watched CAN message's value.
enum class WatchdogDecodeMode : uint8_t {
    RAW_BYTE   = 0,  ///< Single byte as value
    RAW_BYTES  = 1,  ///< Byte range as big-endian unsigned integer
    FORMULA    = 2,  ///< Linear formula: (raw * scale + offset) / divisor
};

/// Decode configuration for a single watchdog entry.
struct WatchdogConfig {
    WatchdogDecodeMode mode = WatchdogDecodeMode::RAW_BYTE;
    uint8_t  byte_start  = 0;        ///< Start byte index (0ÔÇô7)
    uint8_t  byte_count  = 1;        ///< Number of bytes (1ÔÇô8)
    double   scale       = 1.0;
    double   offset      = 0.0;
    double   divisor     = 1.0;
};

/// A single history sample for a watchdog entry.
struct WatchdogHistoryEntry {
    double   timestamp = 0.0;   ///< Session-relative seconds
    double   value     = 0.0;   ///< Decoded value
};

/// Per-ID state for change detection.
struct PerIdState {
    uint32_t arb_id        = 0;
    uint8_t  dlc           = 0;
    std::array<uint8_t, 8> data      = {};
    std::deque<std::array<uint8_t, 8>> history;     // last N payloads
    std::deque<double>                 history_time; // last N timestamps (session-relative)
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

/// Snapshot of a watchdog ID's decoded state + history.
struct WatchdogSnapshot {
    uint32_t arb_id     = 0;
    WatchdogConfig config;
    double   current_value = 0.0;
    std::vector<WatchdogHistoryEntry> history;
    MessageRow row;
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

    /// Take a filtered snapshot for rendering (grouped by ID).
    std::vector<MessageRow> snapshot(bool changed_only, uint32_t change_n,
                                     ObdDisplayMode obd_mode,
                                     IdFilterMode id_filter_mode = IdFilterMode::EXCLUDE,
                                     const std::vector<uint32_t>& id_filter_list = {}) const;

    /// Take a raw chronological snapshot from the ring buffer (most recent N).
    std::vector<MessageRow> raw_snapshot(uint32_t max_rows = 5000) const;

    /// Take a watchdog-only snapshot.
    std::vector<MessageRow> watchdog_snapshot() const;

    // Watchdog management
    void add_watchdog(uint32_t id);
    void remove_watchdog(uint32_t id);
    void clear_watchdogs();
    bool is_watched(uint32_t id) const;

    // Watchdog decode configuration
    void set_watchdog_config(uint32_t id, const WatchdogConfig& cfg);
    WatchdogConfig get_watchdog_config(uint32_t id) const;

    /// Get detailed watchdog snapshots including decoded history.
    std::vector<WatchdogSnapshot> watchdog_detail_snapshot() const;

    /// Get byte value history for a specific CAN ID and byte index.
    /// Returns (timestamp, byte_value) pairs for graphing.
    std::vector<std::pair<double, double>> byte_history(uint32_t arb_id, uint8_t byte_idx) const;

    /// Set max history samples per watchdog (called when settings change).
    void set_watchdog_history_size(uint32_t max_samples);

    // Buffer info
    uint64_t buffer_count() const;
    uint32_t buffer_capacity() const;
    void resize_buffer(uint32_t new_cap);

    /// Set whether the ring buffer overwrites oldest frames when full.
    /// When false, new frames are dropped once the buffer is full.
    void set_overwrite(bool allow);
    bool is_buffer_full() const;

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
    bool   overwrite_  = false;

    // Per-ID state
    std::unordered_map<uint32_t, PerIdState> ids_;

    // Watchdog
    std::unordered_set<uint32_t> watchdog_ids_;
    std::unordered_map<uint32_t, WatchdogConfig> watchdog_configs_;
    std::unordered_map<uint32_t, std::deque<WatchdogHistoryEntry>> watchdog_history_;
    uint32_t watchdog_history_max_ = 200;
};

} // namespace canmatik
