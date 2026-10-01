#include "core/codec/Lzw.h"

#include <array>

namespace lab2 {

namespace {

class BitStream {
public:
    BitStream(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    int read(int count) {
        while (bitCount_ < count) {
            if (position_ >= size_) {
                exhausted_ = true;
                return -1;
            }
            bitBuffer_ |= static_cast<int>(data_[position_++]) << bitCount_;
            bitCount_ += 8;
        }
        const int value = bitBuffer_ & ((1 << count) - 1);
        bitBuffer_ >>= count;
        bitCount_ -= count;
        return value;
    }

    bool exhausted() const { return exhausted_; }
    std::size_t position() const { return position_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    int bitBuffer_ = 0;
    int bitCount_ = 0;
    bool exhausted_ = false;
};

}

LzwResult lzwDecodeGif(const std::uint8_t* data, std::size_t size, int minimumCodeSize,
                       std::size_t maxOutput, std::vector<std::uint8_t>& out) {
    LzwResult result;
    out.clear();
    if (minimumCodeSize < 2 || minimumCodeSize > 11) {
        result.error = "недопустимый минимальный размер кода LZW";
        return result;
    }

    const int clearCode = 1 << minimumCodeSize;
    const int endCode = clearCode + 1;
    std::array<std::uint16_t, 4096> prefix{};
    std::array<std::uint8_t, 4096> suffix{};
    std::array<std::uint8_t, 4096> stack{};
    for (int i = 0; i < clearCode; ++i) {
        prefix[static_cast<std::size_t>(i)] = 0xFFFF;
        suffix[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    }

    BitStream stream(data, size);
    int codeSize = minimumCodeSize + 1;
    int nextCode = endCode + 1;
    int previous = -1;
    int stackTop = 0;

    while (true) {
        const int code = stream.read(codeSize);
        if (code < 0) {
            break;
        }
        if (code == endCode) {
            result.ok = true;
            result.bytesConsumed = stream.position();
            return result;
        }
        if (code == clearCode) {
            codeSize = minimumCodeSize + 1;
            nextCode = endCode + 1;
            previous = -1;
            continue;
        }

        int current = code;
        if (code > nextCode || (code == nextCode && previous < 0)) {
            result.error = "ссылка LZW на несуществующий код";
            return result;
        }
        if (code == nextCode) {
            stack[static_cast<std::size_t>(stackTop++)] = suffix[static_cast<std::size_t>(previous)];
            current = previous;
        }
        int walk = current;
        while (prefix[static_cast<std::size_t>(walk)] != 0xFFFF && walk < 4096) {
            if (stackTop >= 4096) {
                result.error = "переполнение стека LZW";
                return result;
            }
            stack[static_cast<std::size_t>(stackTop++)] = suffix[static_cast<std::size_t>(walk)];
            walk = prefix[static_cast<std::size_t>(walk)];
        }
        if (stackTop >= 4096) {
            result.error = "переполнение стека LZW";
            return result;
        }
        stack[static_cast<std::size_t>(stackTop++)] = suffix[static_cast<std::size_t>(walk)];

        if (previous >= 0 && nextCode < 4096) {
            prefix[static_cast<std::size_t>(nextCode)] = static_cast<std::uint16_t>(previous);
            suffix[static_cast<std::size_t>(nextCode)] = suffix[static_cast<std::size_t>(walk)];
            ++nextCode;
            if (nextCode == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }

        while (stackTop > 0) {
            if (out.size() + 1 > maxOutput) {
                result.error = "распакованный размер превышает ожидаемый";
                return result;
            }
            out.push_back(stack[static_cast<std::size_t>(--stackTop)]);
        }
        previous = code;
        if (stream.exhausted()) {
            break;
        }
    }

    result.ok = !out.empty();
    result.bytesConsumed = stream.position();
    if (!result.ok) {
        result.error = "поток LZW закончился без кода конца данных";
    }
    return result;
}

}