#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "model/ImageFormat.h"

namespace lab2 {

struct Detection {
    Format format = Format::Unknown;
    std::string signature;
};

Detection detectFormat(const std::uint8_t* data, std::size_t size);

}