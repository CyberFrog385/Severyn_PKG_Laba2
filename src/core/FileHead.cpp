#include "core/FileHead.h"

#include <algorithm>

namespace lab2 {

namespace {

#ifdef _WIN32
std::FILE* openBinary(const std::filesystem::path& path) {
    return _wfopen(path.c_str(), L"rb");
}
int seekTo(std::FILE* handle, long long offset) { return _fseeki64(handle, offset, SEEK_SET); }
int seekEnd(std::FILE* handle) { return _fseeki64(handle, 0, SEEK_END); }
long long tellOf(std::FILE* handle) { return _ftelli64(handle); }
#else
std::FILE* openBinary(const std::filesystem::path& path) { return std::fopen(path.c_str(), "rb"); }
int seekTo(std::FILE* handle, long long offset) { return fseeko(handle, static_cast<off_t>(offset), SEEK_SET); }
int seekEnd(std::FILE* handle) { return fseeko(handle, 0, SEEK_END); }
long long tellOf(std::FILE* handle) { return ftello(handle); }
#endif

}

FileHead::~FileHead() { close(); }

FileHead::FileHead(FileHead&& other) noexcept
    : handle_(other.handle_),
      path_(std::move(other.path_)),
      size_(other.size_),
      ranges_(std::move(other.ranges_)),
      error_(std::move(other.error_)) {
    other.handle_ = nullptr;
    other.size_ = 0;
    other.ranges_.clear();
}

FileHead& FileHead::operator=(FileHead&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        path_ = std::move(other.path_);
        size_ = other.size_;
        ranges_ = std::move(other.ranges_);
        error_ = std::move(other.error_);
        other.handle_ = nullptr;
        other.size_ = 0;
        other.ranges_.clear();
    }
    return *this;
}

bool FileHead::open(const std::filesystem::path& path) {
    close();
    path_ = path;
    error_.clear();
    std::FILE* handle = openBinary(path);
    if (handle == nullptr) {
        error_ = "не удалось открыть файл";
        return false;
    }
    if (seekEnd(handle) != 0) {
        std::fclose(handle);
        error_ = "не удалось перейти в конец файла";
        return false;
    }
    const long long size = tellOf(handle);
    if (size < 0 || seekTo(handle, 0) != 0) {
        std::fclose(handle);
        error_ = "не удалось определить размер файла";
        return false;
    }
    handle_ = handle;
    size_ = static_cast<std::uint64_t>(size);
    ranges_.clear();
    return true;
}

void FileHead::markRead(std::uint64_t offset, std::uint64_t count) {
    if (count == 0) {
        return;
    }
    ranges_.emplace_back(offset, offset + count);
    if (ranges_.size() <= 64) {
        return;
    }
    std::sort(ranges_.begin(), ranges_.end());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> merged;
    for (const auto& range : ranges_) {
        if (merged.empty() || range.first > merged.back().second) {
            merged.push_back(range);
        } else if (range.second > merged.back().second) {
            merged.back().second = range.second;
        }
    }
    ranges_ = std::move(merged);
}

std::uint64_t FileHead::bytesRead() const {
    std::uint64_t total = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> sorted = ranges_;
    if (sorted.size() > 1) {
        std::sort(sorted.begin(), sorted.end());
    }
    std::uint64_t currentStart = 0;
    std::uint64_t currentEnd = 0;
    bool has = false;
    for (const auto& range : sorted) {
        if (has && range.first <= currentEnd) {
            currentEnd = std::max(currentEnd, range.second);
        } else {
            if (has) {
                total += currentEnd - currentStart;
            }
            currentStart = range.first;
            currentEnd = range.second;
            has = true;
        }
    }
    if (has) {
        total += currentEnd - currentStart;
    }
    return total;
}

void FileHead::close() {
    if (handle_ != nullptr) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
}

bool FileHead::readAt(std::uint64_t offset, std::size_t count, std::vector<std::uint8_t>& out) {
    out.clear();
    if (handle_ == nullptr) {
        error_ = "файл не открыт";
        return false;
    }
    if (count == 0 || offset >= size_) {
        return false;
    }
    std::size_t want = count;
    if (offset + want > size_) {
        want = static_cast<std::size_t>(size_ - offset);
    }
    out.resize(want);
    if (seekTo(handle_, static_cast<long long>(offset)) != 0) {
        error_ = "ошибка позиционирования в файле";
        return false;
    }
    const std::size_t got = std::fread(out.data(), 1, want, handle_);
    markRead(offset, got);
    if (got != want) {
        out.resize(got);
        return false;
    }
    return true;
}

bool FileHead::readInto(std::uint64_t offset, std::size_t count, std::uint8_t* destination) {
    if (handle_ == nullptr || destination == nullptr) {
        error_ = "файл не открыт";
        return false;
    }
    if (count == 0 || offset >= size_) {
        return false;
    }
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(count, size_ - offset));
    if (seekTo(handle_, static_cast<long long>(offset)) != 0) {
        error_ = "ошибка позиционирования в файле";
        return false;
    }
    const std::size_t got = std::fread(destination, 1, want, handle_);
    markRead(offset, got);
    if (got != want) {
        error_ = "файл закончился раньше ожидаемого";
        return false;
    }
    return true;
}

bool FileHead::readAll(std::vector<std::uint8_t>& out, std::size_t maxBytes) {
    out.clear();
    if (handle_ == nullptr) {
        error_ = "файл не открыт";
        return false;
    }
    const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(size_, maxBytes));
    out.resize(want);
    if (want == 0) {
        return true;
    }
    if (!readInto(0, want, out.data())) {
        out.clear();
        return false;
    }
    return true;
}

bool FileHead::readTail(std::size_t count, std::vector<std::uint8_t>& out) {
    out.clear();
    if (handle_ == nullptr || size_ == 0) {
        return false;
    }
    const std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>(count, size_));
    return readAt(size_ - want, want, out);
}

}