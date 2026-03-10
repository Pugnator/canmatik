#pragma once

/// @file capture_service.h
/// CaptureService — dedicated reader thread, SPSC ring buffer, ICaptureSync dispatch (T032 — US2).

#include "core/can_frame.h"
#include "core/capture_sink.h"
#include "core/filter.h"
#include "core/session_status.h"
#include "transport/channel.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace canmatik {

/// Lock-free single-producer single-consumer ring buffer for CanFrame.
/// Capacity is fixed at construction (must be power of two).
/// Drop-oldest semantics on overflow.
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity);

    /// Push a frame. If full, drops the oldest frame and returns true.
    /// @return true if a frame was dropped due to overflow.
    bool push(const CanFrame& frame);

    /// Pop a frame. Returns false if the queue is empty.
    bool pop(CanFrame& frame);

    /// Number of frames currently in the queue (approximate).
    [[nodiscard]] size_t size() const;

    [[nodiscard]] size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    size_t mask_;
    std::vector<CanFrame> buffer_;
    alignas(64) std::atomic<size_t> head_{0}; // producer writes here
    alignas(64) std::atomic<size_t> tail_{0}; // consumer reads here
};

/// CaptureService — reads frames from IChannel on a dedicated thread,
/// enqueues them into an SPSC ring buffer, and dispatches to registered
/// ICaptureSync observers from the consumer side.
class CaptureService {
public:
    static constexpr size_t kDefaultQueueCapacity = 65536;
    static constexpr uint32_t kDefaultReadTimeoutMs = 50;

    CaptureService();
    ~CaptureService();

    // Non-copyable, non-movable
    CaptureService(const CaptureService&) = delete;
    CaptureService& operator=(const CaptureService&) = delete;

    /// Register an observer to receive frames and errors.
    void addSink(ICaptureSync* sink);

    /// Remove a previously registered observer.
    void removeSink(ICaptureSync* sink);

    /// Set the filter engine for display filtering.
    void setFilter(const FilterEngine& filter);

    /// Start capturing from the given channel. Updates status counters.
    /// @param channel  Open CAN channel to read from.
    /// @param status   Mutable session status for counter updates.
    void start(IChannel* channel, SessionStatus& status);

    /// Stop capturing. Joins the reader thread and drains remaining frames.
    void stop();

    /// Check whether the capture is currently running.
    [[nodiscard]] bool isRunning() const { return running_.load(); }

    /// Consume available frames from the queue and dispatch to sinks.
    /// Call from the consumer thread (e.g., main thread).
    /// @return Number of frames dispatched.
    size_t drain();

private:
    void readerLoop(IChannel* channel, SessionStatus& status);

    SpscQueue queue_{kDefaultQueueCapacity};
    FilterEngine filter_;
    std::vector<ICaptureSync*> sinks_;
    std::atomic<bool> running_{false};
    std::thread reader_thread_;
    SessionStatus* status_ = nullptr;
};

} // namespace canmatik
