#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lab2 {

struct LzwResult {
    bool ok = false;
    std::string error;
    std::size_t bytesConsumed = 0;
};

LzwResult lzwDecodeGif(const std::uint8_t* data, std::size_t size, int minimumCodeSize,
                       std::size_t maxOutput, std::vector<std::uint8_t>& out);

}