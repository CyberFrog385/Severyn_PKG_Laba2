#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace lab2 {

class ByteReader {
public:
    ByteReader() = default;

    ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return size_; }
    std::size_t remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }
    bool ok() const { return ok_; }
    const std::uint8_t* data() const { return data_; }

    void reset() {
        pos_ = 0;
        ok_ = true;
    }

    bool seek(std::size_t offset) {
        pos_ = offset;
        ok_ = offset <= size_;
        return ok_;
    }

    bool skip(std::size_t count) {
        pos_ += count;
        ok_ = pos_ <= size_;
        return ok_;
    }

    bool has(std::size_t count) const { return pos_ + count <= size_; }

    const std::uint8_t* pointer() {
        if (!has(1)) {
            ok_ = false;
            return nullptr;
        }
        return data_ + pos_;
    }

    std::uint8_t u8() {
        if (!has(1)) {
            ok_ = false;
            return 0;
        }
        return data_[pos_++];
    }

    std::uint8_t peek(std::size_t ahead = 0) const {
        if (pos_ + ahead >= size_) {
            return 0;
        }
        return data_[pos_ + ahead];
    }

    std::uint16_t u16le() {
        const std::uint8_t* p = need(2);
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    std::uint16_t u16be() {
        const std::uint8_t* p = need(2);
        return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
    }

    std::uint32_t u32le() {
        const std::uint8_t* p = need(4);
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    std::uint32_t u32be() {
        const std::uint8_t* p = need(4);
        return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
               (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    }

    std::uint64_t u64le() {
        const std::uint32_t low = u32le();
        const std::uint32_t high = u32le();
        return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
    }

    std::uint64_t u64be() {
        const std::uint32_t high = u32be();
        const std::uint32_t low = u32be();
        return (static_cast<std::uint64_t>(high) << 32) | low;
    }

    std::uint16_t u16(bool littleEndian) { return littleEndian ? u16le() : u16be(); }

    std::uint32_t u32(bool littleEndian) { return littleEndian ? u32le() : u32be(); }

    std::uint64_t u64(bool littleEndian) { return littleEndian ? u64le() : u64be(); }

    std::string fixedString(std::size_t count) {
        const std::uint8_t* p = need(count);
        std::string text;
        text.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const char c = static_cast<char>(p[i]);
            text.push_back(c == 0 ? ' ' : c);
        }
        return text;
    }

    std::string rawString(std::size_t count) {
        const std::uint8_t* p = need(count);
        return std::string(reinterpret_cast<const char*>(p), count);
    }

    bool matchSignature(const std::uint8_t* signature, std::size_t count) const {
        return size_ >= count && std::memcmp(data_, signature, count) == 0;
    }

private:
    const std::uint8_t* need(std::size_t count) {
        if (!has(count)) {
            ok_ = false;
            pos_ = size_;
            return zero_;
        }
        const std::uint8_t* p = data_ + pos_;
        pos_ += count;
        return p;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t pos_ = 0;
    bool ok_ = true;
    std::uint8_t zero_[8] = {};
};

}