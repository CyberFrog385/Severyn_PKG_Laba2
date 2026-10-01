#pragma once

#include <cstdint>
#include <string>

namespace lab2 {

struct TiffTag {
    std::uint16_t id = 0;
    const char* name = "";
};

const char* tiffTagName(std::uint16_t id);
const char* tiffCompressionName(std::uint16_t code);
const char* tiffPhotometricName(std::uint16_t code);
const char* tiffResolutionUnitName(std::uint16_t code);
const char* tiffPredictorName(std::uint16_t code);
std::uint32_t tiffTypeSize(std::uint16_t type);
const char* tiffTypeName(std::uint16_t type);

}