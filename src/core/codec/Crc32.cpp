#include "core/codec/Crc32.h"

#include <array>

namespace lab2 {

namespace {

std::array<std::uint32_t, 256> makeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
        }
        table[i] = value;
    }
    return table;
}

const std::array<std::uint32_t, 256>& table() {
    static const std::array<std::uint32_t, 256> data = makeTable();
    return data;
}

}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed) {
    const auto& lookup = table();
    std::uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = lookup[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

}