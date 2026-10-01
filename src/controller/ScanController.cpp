#include "controller/ScanController.h"

#include <algorithm>

#include "core/MetaExtractor.h"
#include "core/parsers/ParserCommon.h"
#include "io/DirectoryScanner.h"
#include "model/ImageFormat.h"

namespace lab2 {

ScanController::~ScanController() {
    cancel();
    if (coordinator_.joinable()) {
        coordinator_.join();
    }
}

void ScanController::start(const std::filesystem::path& root, const ScanOptions& options) {
    std::vector<std::filesystem::path> files{root};
    start(files, options);
}

void ScanController::start(const std::vector<std::filesystem::path>& paths,
                           const ScanOptions& options) {
    cancel();
    if (coordinator_.joinable()) {
        coordinator_.join();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.clear();
        delivered_ = 0;
    }
    counters_.total.store(0, std::memory_order_relaxed);
    counters_.done.store(0, std::memory_order_relaxed);
    counters_.corrupted.store(0, std::memory_order_relaxed);
    counters_.notImage.store(0, std::memory_order_relaxed);
    counters_.warnings.store(0, std::memory_order_relaxed);
    counters_.bytesRead.store(0, std::memory_order_relaxed);
    counters_.fileBytes.store(0, std::memory_order_relaxed);
    counters_.cancelRequested.store(false, std::memory_order_relaxed);
    counters_.workers.store(options.workers > 0 ? options.workers : recommendedWorkerCount(),
                            std::memory_order_relaxed);
    markStarted(counters_);
    counters_.running.store(true, std::memory_order_relaxed);

    coordinator_ = std::thread([this, paths, options] {
        std::vector<std::filesystem::path> files;
        EnumerationOptions enumerationOptions;
        enumerationOptions.recursive = options.recursive;
        enumerationOptions.extensions = options.extensions;
        std::uint64_t totalBytes = 0;
        for (const std::filesystem::path& path : paths) {
            const Enumeration enumeration =
                enumeratePath(path, enumerationOptions, counters_.cancelRequested);
            totalBytes += enumeration.totalBytes;
            files.insert(files.end(), enumeration.files.begin(), enumeration.files.end());
        }
        counters_.fileBytes.store(totalBytes, std::memory_order_relaxed);
        counters_.total.store(files.size(), std::memory_order_relaxed);
        if (files.empty()) {
            finish();
            return;
        }
        ThreadPool pool(options.workers);
        pool.run(files.size(), [&](std::size_t index) { analyzeOne(files[index], options); });
        finish();
    });
}

void ScanController::cancel() {
    counters_.cancelRequested.store(true, std::memory_order_relaxed);
}

void ScanController::finish() { counters_.running.store(false, std::memory_order_relaxed); }

void ScanController::analyzeOne(const std::filesystem::path& path, const ScanOptions& options) {
    if (counters_.cancelRequested.load(std::memory_order_relaxed)) {
        return;
    }
    ImageRecord record;
    record.path = path;
    record.name = path.filename().string();
    record.folder = path.parent_path().string();
    record.extensionName = path.extension().string();
    std::error_code sizeError;
    record.fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(path, sizeError));

    FileHead file;
    if (!file.open(path)) {
        record.format = Format::NotImage;
        record.check = CheckState::Corrupted;
        record.problems.push_back("не удалось открыть файл: " + file.lastError());
        record.bytesRead = 0;
    } else {
        extractMeta(file, record);
        if (options.deepAnalysis && !record.hasError()) {
            const DecodeResult decoded = decodeImage(file, record, options.limits);
            record.pixelsAnalyzed = true;
            record.pixels = decoded.stats;
            record.pixels.decoder = decoded.decoder;
            if (!decoded.ok) {
                record.pixels.error = decoded.error;
                if (decoded.unsupported || decoded.limited) {
                    addProblem(record, "Глубокий анализ: " + decoded.error, CheckState::Warning);
                } else {
                    addProblem(record, "Декодирование пикселей не удалось: " + decoded.error,
                               CheckState::Corrupted);
                }
            }
        }
        record.bytesRead = file.bytesRead();
    }

    if (record.check == CheckState::Corrupted) {
        counters_.corrupted.fetch_add(1, std::memory_order_relaxed);
    } else if (record.check == CheckState::Warning) {
        counters_.warnings.fetch_add(1, std::memory_order_relaxed);
    } else if (record.check == CheckState::NotImage) {
        counters_.notImage.fetch_add(1, std::memory_order_relaxed);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back(std::move(record));
    }
    counters_.bytesRead.fetch_add(record.bytesRead, std::memory_order_relaxed);
    counters_.done.fetch_add(1, std::memory_order_relaxed);
}

ScanSummary ScanController::progress() const { return lab2::snapshot(counters_); }

std::vector<ImageRecord> ScanController::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (delivered_ >= results_.size()) {
        return {};
    }
    std::vector<ImageRecord> batch(results_.begin() + static_cast<std::ptrdiff_t>(delivered_),
                                   results_.end());
    delivered_ = results_.size();
    return batch;
}

std::vector<ImageRecord> ScanController::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_;
}

void ScanController::clearResults() {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.clear();
    delivered_ = 0;
}

}