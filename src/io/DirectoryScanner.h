#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lab2 {

struct EnumerationOptions {
    bool recursive = true;
    bool followSymlinks = false;
    std::vector<std::string> extensions;
};

struct Enumeration {
    std::vector<std::filesystem::path> files;
    std::uint64_t totalBytes = 0;
    std::size_t skippedErrors = 0;
    bool cancelled = false;
    std::string error;
};

Enumeration enumeratePath(const std::filesystem::path& root, const EnumerationOptions& options,
                          const std::atomic<bool>& cancel);

}