#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lab2 {

struct RleResult {
    bool ok = false;
    std::string error;
};

RleResult decodeBmpRle8(const std::uint8_t* data, std::size_t size, int width, int height,
                        std::vector<std::uint8_t>& out);

RleResult decodeBmpRle4(const std::uint8_t* data, std::size_t size, int width, int height,
                        std::vector<std::uint8_t>& out);

RleResult decodePcxRle(const std::uint8_t* data, std::size_t size, std::size_t expected,
                       std::vector<std::uint8_t>& out);

}