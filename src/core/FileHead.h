#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace lab2 {

class FileHead {
public:
    FileHead() = default;
    ~FileHead();

    FileHead(const FileHead&) = delete;
    FileHead& operator=(const FileHead&) = delete;
    FileHead(FileHead&& other) noexcept;
    FileHead& operator=(FileHead&& other) noexcept;

    bool open(const std::filesystem::path& path);
    void close();

    bool isOpen() const { return handle_ != nullptr; }
    const std::filesystem::path& path() const { return path_; }
    std::uint64_t size() const { return size_; }
    std::uint64_t bytesRead() const;
    std::string lastError() const { return error_; }

    bool readAt(std::uint64_t offset, std::size_t count, std::vector<std::uint8_t>& out);
    bool readInto(std::uint64_t offset, std::size_t count, std::uint8_t* destination);
    bool readAll(std::vector<std::uint8_t>& out, std::size_t maxBytes);
    bool readTail(std::size_t count, std::vector<std::uint8_t>& out);

private:
    std::FILE* handle_ = nullptr;
    std::filesystem::path path_;
    std::uint64_t size_ = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges_;
    std::string error_;

    void markRead(std::uint64_t offset, std::uint64_t count);
};

}