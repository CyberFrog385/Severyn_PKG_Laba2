#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/DecoderRegistry.h"
#include "io/ThreadPool.h"
#include "model/ImageRecord.h"
#include "model/ScanState.h"

namespace lab2 {

struct ScanOptions {
    bool recursive = true;
    bool deepAnalysis = false;
    int workers = 0;
    std::vector<std::string> extensions;
    DecodeLimits limits;
};

class ScanController {
public:
    ScanController() = default;
    ~ScanController();

    ScanController(const ScanController&) = delete;
    ScanController& operator=(const ScanController&) = delete;

    void start(const std::filesystem::path& root, const ScanOptions& options);
    void start(const std::vector<std::filesystem::path>& paths, const ScanOptions& options);
    void cancel();

    bool running() const { return counters_.running.load(std::memory_order_relaxed); }
    ScanSummary progress() const;
    std::vector<ImageRecord> drain();
    std::vector<ImageRecord> snapshot() const;
    void clearResults();

private:
    void analyzeOne(const std::filesystem::path& path, const ScanOptions& options);
    void finish();

    mutable std::mutex mutex_;
    ScanCounters counters_;
    std::vector<ImageRecord> results_;
    std::size_t delivered_ = 0;
    std::thread coordinator_;
};

}