#pragma once

#include <cstddef>
#include <cstdint>

namespace lab2 {

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed = 0);

}