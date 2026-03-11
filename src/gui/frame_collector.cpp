/// @file frame_collector.cpp
/// Thread-safe frame collector implementation.

#include "gui/frame_collector.h"

#include <algorithm>

namespace canmatik {

FrameCollector::FrameCollector(uint64_t session_start_us, uint32_t capacity)
    : session_start_us_(session_start_us), ring_(capacity) {}

void FrameCollector::onFrame(const CanFrame& frame) {
    std::lock_guard lock(mu_);
    ingest(frame);
}

void FrameCollector::onError(const TransportError& /*error*/) {
    // Errors tracked via GuiState counters, not here.
}

void FrameCollector::pushFrame(const CanFrame& frame) {
    std::lock_guard lock(mu_);
    ingest(frame);
}

void FrameCollector::ingest(const CanFrame& frame) {
    // Store in ring buffer
    if (!ring_.empty()) {
        ring_[ring_head_] = frame;
        ring_head_ = (ring_head_ + 1) % ring_.size();
        if (ring_count_ < ring_.size()) ++ring_count_;
    }

    // Update per-ID state
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us_);
    uint8_t n = (frame.dlc <= 8) ? frame.dlc : uint8_t{8};

    auto [it, inserted] = ids_.try_emplace(frame.arbitration_id);
    PerIdState& s = it->second;

    if (inserted) {
        s.arb_id      = frame.arbitration_id;
        s.dlc         = frame.dlc;
        s.is_new      = true;
        s.dlc_changed = false;
        for (uint8_t i = 0; i < n; ++i) {
            s.data[i]    = frame.data[i];
            s.changed[i] = false;
        }
        s.last_seen    = rel;
        s.update_count = 1;
        // Push to history
        std::array<uint8_t, 8> h{};
        for (uint8_t i = 0; i < n; ++i) h[i] = frame.data[i];
        s.history.push_back(h);
        s.history_dlc.push_back(frame.dlc);
        return;
    }

    // Detect changes
    bool any_change = false;
    s.dlc_changed = (frame.dlc != s.dlc);
    if (s.dlc_changed) any_change = true;

    for (uint8_t i = 0; i < 8; ++i) {
        if (i < n) {
            bool diff = (i >= s.dlc) || (frame.data[i] != s.data[i]);
            s.changed[i] = diff;
            if (diff) any_change = true;
        } else {
            s.changed[i] = false;
        }
    }

    if (!any_change) {
        s.update_count++;
        s.last_seen = rel;
        return;
    }

    // Apply update
    s.is_new = false;
    s.dlc    = frame.dlc;
    for (uint8_t i = 0; i < n; ++i) s.data[i] = frame.data[i];
    s.last_seen = rel;
    s.update_count++;

    // Push to history (cap at 100)
    std::array<uint8_t, 8> h{};
    for (uint8_t i = 0; i < n; ++i) h[i] = frame.data[i];
    s.history.push_back(h);
    s.history_dlc.push_back(frame.dlc);
    if (s.history.size() > 100) {
        s.history.pop_front();
        s.history_dlc.pop_front();
    }
}

bool FrameCollector::is_obd_id(uint32_t id) {
    return id == 0x7DF || (id >= 0x7E0 && id <= 0x7EF);
}

MessageRow FrameCollector::to_row(const PerIdState& s) const {
    MessageRow r;
    r.arb_id       = s.arb_id;
    r.dlc          = s.dlc;
    r.data         = s.data;
    r.changed      = s.changed;
    r.is_new       = s.is_new;
    r.dlc_changed  = s.dlc_changed;
    r.last_seen    = s.last_seen;
    r.update_count = s.update_count;
    r.is_watched   = watchdog_ids_.count(s.arb_id) > 0;
    return r;
}

std::vector<MessageRow> FrameCollector::snapshot(bool changed_only, uint32_t change_n,
                                                  ObdDisplayMode obd_mode,
                                                  IdFilterMode id_filter_mode,
                                                  const std::vector<uint32_t>& id_filter_list) const {
    std::lock_guard lock(mu_);
    std::vector<MessageRow> out;
    out.reserve(ids_.size());

    // Build a fast lookup set for the ID filter list
    std::unordered_set<uint32_t> id_set(id_filter_list.begin(), id_filter_list.end());

    for (auto& [id, s] : ids_) {
        // ID include/exclude filter
        if (!id_set.empty()) {
            bool in_list = id_set.count(id) > 0;
            if (id_filter_mode == IdFilterMode::INCLUDE && !in_list) continue;
            if (id_filter_mode == IdFilterMode::EXCLUDE && in_list) continue;
        }

        // OBD mode filter
        bool is_obd = is_obd_id(id);
        if (obd_mode == ObdDisplayMode::OBD_ONLY && !is_obd) continue;
        if (obd_mode == ObdDisplayMode::BROADCAST_ONLY && is_obd) continue;

        // Changed-only filter
        if (changed_only && !s.is_new) {
            // Check if any of the last change_n transmissions differ
            if (s.history.size() <= 1) continue; // only one sample, no change
            bool found_change = false;
            size_t hist_size = s.history.size();
            size_t check_count = std::min(static_cast<size_t>(change_n), hist_size - 1);
            const auto& latest = s.history.back();
            for (size_t j = 0; j < check_count; ++j) {
                const auto& prev = s.history[hist_size - 2 - j];
                for (uint8_t b = 0; b < 8; ++b) {
                    if (latest[b] != prev[b]) { found_change = true; break; }
                }
                if (found_change) break;
            }
            if (!found_change && !s.dlc_changed) continue;
        }

        out.push_back(to_row(s));
    }

    std::sort(out.begin(), out.end(), [](const MessageRow& a, const MessageRow& b) {
        return a.arb_id < b.arb_id;
    });
    return out;
}

std::vector<MessageRow> FrameCollector::watchdog_snapshot() const {
    std::lock_guard lock(mu_);
    std::vector<MessageRow> out;
    for (auto& [id, s] : ids_) {
        if (watchdog_ids_.count(id) > 0)
            out.push_back(to_row(s));
    }
    std::sort(out.begin(), out.end(), [](const MessageRow& a, const MessageRow& b) {
        return a.arb_id < b.arb_id;
    });
    return out;
}

void FrameCollector::add_watchdog(uint32_t id) {
    std::lock_guard lock(mu_);
    watchdog_ids_.insert(id);
}

void FrameCollector::remove_watchdog(uint32_t id) {
    std::lock_guard lock(mu_);
    watchdog_ids_.erase(id);
}

void FrameCollector::clear_watchdogs() {
    std::lock_guard lock(mu_);
    watchdog_ids_.clear();
}

bool FrameCollector::is_watched(uint32_t id) const {
    std::lock_guard lock(mu_);
    return watchdog_ids_.count(id) > 0;
}

uint64_t FrameCollector::buffer_count() const {
    std::lock_guard lock(mu_);
    return ring_count_;
}

uint32_t FrameCollector::buffer_capacity() const {
    std::lock_guard lock(mu_);
    return static_cast<uint32_t>(ring_.size());
}

void FrameCollector::resize_buffer(uint32_t new_cap) {
    std::lock_guard lock(mu_);
    if (new_cap == ring_.size()) return;
    // Copy existing frames in chronological order
    std::vector<CanFrame> old_contents;
    if (ring_count_ > 0) {
        old_contents.reserve(ring_count_);
        size_t start = (ring_count_ < ring_.size()) ? 0 : ring_head_;
        for (size_t i = 0; i < ring_count_; ++i) {
            old_contents.push_back(ring_[(start + i) % ring_.size()]);
        }
    }
    ring_.resize(new_cap);
    ring_head_  = 0;
    ring_count_ = 0;
    // Re-insert (keep latest if old data exceeds new capacity)
    size_t skip = 0;
    if (old_contents.size() > new_cap) skip = old_contents.size() - new_cap;
    for (size_t i = skip; i < old_contents.size(); ++i) {
        ring_[ring_head_] = old_contents[i];
        ring_head_ = (ring_head_ + 1) % ring_.size();
        ++ring_count_;
    }
}

std::vector<CanFrame> FrameCollector::buffer_contents() const {
    std::lock_guard lock(mu_);
    std::vector<CanFrame> out;
    out.reserve(ring_count_);
    if (ring_count_ == 0) return out;
    size_t start = (ring_count_ < ring_.size()) ? 0 : ring_head_;
    for (size_t i = 0; i < ring_count_; ++i) {
        out.push_back(ring_[(start + i) % ring_.size()]);
    }
    return out;
}

void FrameCollector::clear() {
    std::lock_guard lock(mu_);
    ring_head_  = 0;
    ring_count_ = 0;
    ids_.clear();
}

uint64_t FrameCollector::unique_ids() const {
    std::lock_guard lock(mu_);
    return ids_.size();
}

} // namespace canmatik
