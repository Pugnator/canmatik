#include "services/global_capture.h"

#include "core/log_macros.h"
#include <mutex>
#include <unordered_set>

namespace canmatik {

namespace {
    CaptureService& g_capture() {
        static CaptureService inst;
        return inst;
    }

    // Track registered sinks so we can stop the capture service only when
    // the last sink is removed. Protect with a mutex for thread-safety.
    std::mutex g_mu;
    std::unordered_set<ICaptureSync*> g_sinks;
}

void AddGlobalSink(ICaptureSync* sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_sinks.insert(sink).second) {
        g_capture().addSink(sink);
    }
}

void RemoveGlobalSink(ICaptureSync* sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_sinks.find(sink);
    if (it != g_sinks.end()) {
        g_capture().removeSink(sink);
        g_sinks.erase(it);
    }
    if (g_sinks.empty()) {
        if (g_capture().isRunning()) {
            LOG_INFO("No global sinks remain; stopping capture service");
            g_capture().stop();
        }
    }
}

void StartGlobalCapture(IChannel* channel, SessionStatus& status) {
    if (!g_capture().isRunning()) {
        LOG_INFO("Starting global capture service");
        g_capture().start(channel, status);
    } else {
        LOG_DEBUG("Global capture service already running");
    }
}

void StopGlobalCapture() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_sinks.empty()) {
        LOG_DEBUG("Global capture not stopped: {} sinks remain", g_sinks.size());
        return;
    }
    if (g_capture().isRunning()) {
        LOG_INFO("Stopping global capture service");
        g_capture().stop();
    }
}

void StopGlobalCaptureForced() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_capture().isRunning()) {
        LOG_INFO("Force-stopping global capture service (forced)");
        g_capture().stop();
    }
}

void DrainGlobalCapture() {
    g_capture().drain();
}

void SetGlobalFilter(const FilterEngine& filter) {
    g_capture().setFilter(filter);
}

bool IsGlobalCaptureRunning() {
    return g_capture().isRunning();
}

} // namespace canmatik
