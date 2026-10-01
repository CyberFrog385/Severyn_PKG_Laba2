#include "io/DirectoryScanner.h"

#include <algorithm>
#include <system_error>

namespace lab2 {

namespace {

bool matchesExtension(const std::filesystem::path& path, const std::vector<std::string>& extensions) {
    if (extensions.empty()) {
        return true;
    }
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

}

Enumeration enumeratePath(const std::filesystem::path& root, const EnumerationOptions& options,
                          const std::atomic<bool>& cancel) {
    Enumeration result;
    std::error_code error;
    if (root.empty() || !std::filesystem::exists(root, error) || error) {
        result.error = "путь не найден: " + root.string();
        return result;
    }

    if (!std::filesystem::is_directory(root, error) || error) {
        std::error_code sizeError;
        const std::uintmax_t size = std::filesystem::file_size(root, sizeError);
        result.files.push_back(root);
        result.totalBytes = sizeError ? 0 : static_cast<std::uint64_t>(size);
        return result;
    }

    const auto add = [&](const std::filesystem::path& path) {
        if (!matchesExtension(path, options.extensions)) {
            return;
        }
        std::error_code sizeError;
        const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
        result.totalBytes += sizeError ? 0 : static_cast<std::uint64_t>(size);
        result.files.push_back(path);
    };

    if (options.recursive) {
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        if (error) {
            result.error = "не удалось прочитать папку: " + error.message();
            return result;
        }
        const std::filesystem::recursive_directory_iterator end;
        while (iterator != end) {
            if (cancel.load(std::memory_order_relaxed)) {
                result.cancelled = true;
                break;
            }
            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code entryError;
            if (entry.is_regular_file(entryError)) {
                add(entry.path());
            }
            iterator.increment(error);
            if (error) {
                ++result.skippedErrors;
                error.clear();
            }
        }
    } else {
        std::filesystem::directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        if (error) {
            result.error = "не удалось прочитать папку: " + error.message();
            return result;
        }
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            if (cancel.load(std::memory_order_relaxed)) {
                result.cancelled = true;
                break;
            }
            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code entryError;
            if (entry.is_regular_file(entryError)) {
                add(entry.path());
            }
            iterator.increment(error);
            if (error) {
                ++result.skippedErrors;
                error.clear();
            }
        }
    }

    std::sort(result.files.begin(), result.files.end());
    return result;
}

}