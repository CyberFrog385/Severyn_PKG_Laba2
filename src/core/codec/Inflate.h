#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lab2 {

struct InflateResult {
    bool ok = false;
    std::string error;
    std::size_t bytesConsumed = 0;
    bool checksumVerified = false;
};

InflateResult inflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                      std::size_t maxOutput);

InflateResult zlibInflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                          std::size_t maxOutput);

}