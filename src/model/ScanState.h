#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace lab2 {

struct ScanCounters {
    std::atomic<std::size_t> total{0};
    std::atomic<std::size_t> done{0};
    std::atomic<std::size_t> corrupted{0};
    std::atomic<std::size_t> notImage{0};
    std::atomic<std::size_t> warnings{0};
    std::atomic<std::uint64_t> bytesRead{0};
    std::atomic<std::uint64_t> fileBytes{0};
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::atomic<int> workers{0};
    std::atomic<std::int64_t> startedAtTicks{0};
};

std::int64_t steadyTicks();
void markStarted(ScanCounters& counters);

struct ScanSummary {
    std::size_t total = 0;
    std::size_t done = 0;
    std::size_t corrupted = 0;
    std::size_t notImage = 0;
    std::size_t warnings = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t fileBytes = 0;
    int workers = 0;
    bool running = false;
    double seconds = 0.0;
};

ScanSummary snapshot(const ScanCounters& counters);

}