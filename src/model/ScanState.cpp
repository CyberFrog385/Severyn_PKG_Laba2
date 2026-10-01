#include "model/ScanState.h"

namespace lab2 {

std::int64_t steadyTicks() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void markStarted(ScanCounters& counters) {
    counters.startedAtTicks.store(steadyTicks(), std::memory_order_relaxed);
}

ScanSummary snapshot(const ScanCounters& counters) {
    ScanSummary summary;
    summary.total = counters.total.load(std::memory_order_relaxed);
    summary.done = counters.done.load(std::memory_order_relaxed);
    summary.corrupted = counters.corrupted.load(std::memory_order_relaxed);
    summary.notImage = counters.notImage.load(std::memory_order_relaxed);
    summary.warnings = counters.warnings.load(std::memory_order_relaxed);
    summary.bytesRead = counters.bytesRead.load(std::memory_order_relaxed);
    summary.fileBytes = counters.fileBytes.load(std::memory_order_relaxed);
    summary.workers = counters.workers.load(std::memory_order_relaxed);
    summary.running = counters.running.load(std::memory_order_relaxed);
    const std::int64_t started = counters.startedAtTicks.load(std::memory_order_relaxed);
    if (started != 0) {
        summary.seconds = static_cast<double>(steadyTicks() - started) / 1000000.0;
    }
    return summary;
}

}