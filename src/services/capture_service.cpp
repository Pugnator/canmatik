/// @file capture_service.cpp
/// CaptureService implementation — reader thread, SPSC queue, ICaptureSync dispatch (T032 — US2).

#include "services/capture_service.h"
#include "core/log_macros.h"
#include "transport/transport_error.h"

#include <algorithm>
#include <bit>
#include <cassert>

namespace canmatik {

// ---------------------------------------------------------------------------
// SpscQueue
// ---------------------------------------------------------------------------

SpscQueue::SpscQueue(size_t capacity) {
    // Round up to next power of two
    if (capacity == 0) capacity = 1;
    capacity_ = std::bit_ceil(capacity);
    mask_ = capacity_ - 1;
    buffer_.resize(capacity_);
}

bool SpscQueue::push(const CanFrame& frame) {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(std::memory_order_acquire);
    bool dropped = false;

    // If full, advance tail (drop oldest)
    if (h - t >= capacity_) {
        tail_.store(t + 1, std::memory_order_release);
        dropped = true;
    }

    buffer_[h & mask_] = frame;
    head_.store(h + 1, std::memory_order_release);
    return dropped;
}

bool SpscQueue::pop(CanFrame& frame) {
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_acquire);

    if (t >= h) {
        return false; // empty
    }

    frame = buffer_[t & mask_];
    tail_.store(t + 1, std::memory_order_release);
    return true;
}

size_t SpscQueue::size() const {
    size_t h = head_.load(std::memory_order_acquire);
    size_t t = tail_.load(std::memory_order_acquire);
    return h >= t ? h - t : 0;
}

// ---------------------------------------------------------------------------
// CaptureService
// ---------------------------------------------------------------------------

CaptureService::CaptureService() = default;

CaptureService::~CaptureService() {
    stop();
}

void CaptureService::addSink(ICaptureSync* sink) {
    sinks_.push_back(sink);
}

void CaptureService::removeSink(ICaptureSync* sink) {
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

void CaptureService::setFilter(const FilterEngine& filter) {
    filter_ = filter;
}

void CaptureService::start(IChannel* channel, SessionStatus& status) {
    if (running_.load()) {
        LOG_WARNING("CaptureService::start() called while already running");
        return;
    }

    status_ = &status;
    running_.store(true);

    LOG_DEBUG("CaptureService: starting reader thread");
    reader_thread_ = std::thread(&CaptureService::readerLoop, this, channel, std::ref(status));
}

void CaptureService::stop() {
    if (!running_.load()) return;

    LOG_DEBUG("CaptureService: stopping reader thread");
    running_.store(false);

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    // Drain remaining frames
    drain();
    status_ = nullptr;
}

size_t CaptureService::drain() {
    size_t count = 0;
    CanFrame frame;

    while (queue_.pop(frame)) {
        // Apply display filter
        if (!filter_.empty() && !filter_.evaluate(frame.arbitration_id)) {
            continue;
        }

        // Dispatch to all sinks
        for (auto* sink : sinks_) {
            sink->onFrame(frame);
        }
        ++count;
    }
    return count;
}

void CaptureService::readerLoop(IChannel* channel, SessionStatus& status) {
    LOG_DEBUG("CaptureService: reader thread started");

    while (running_.load(std::memory_order_relaxed)) {
        try {
            auto frames = channel->read(kDefaultReadTimeoutMs);

            for (auto& frame : frames) {
                ++status.frames_received;

                if (queue_.push(frame)) {
                    ++status.dropped;
                }
            }
        } catch (const TransportError& e) {
            ++status.errors;
            LOG_ERROR("CaptureService reader: {}", e.what());

            // Dispatch error to sinks
            for (auto* sink : sinks_) {
                sink->onError(e);
            }

            if (!e.recoverable) {
                LOG_ERROR("CaptureService: non-recoverable error, stopping");
                running_.store(false);
                break;
            }
        } catch (const std::exception& e) {
            ++status.errors;
            LOG_ERROR("CaptureService reader: unexpected error: {}", e.what());
            running_.store(false);
            break;
        }
    }

    LOG_DEBUG("CaptureService: reader thread exiting");
}

} // namespace canmatik
