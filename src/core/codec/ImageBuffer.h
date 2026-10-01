#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lab2 {

struct ImageBuffer {
    int width = 0;
    int height = 0;
    int channels = 3;
    std::vector<std::uint8_t> pixels;

    bool valid() const {
        if (width <= 0 || height <= 0 || channels <= 0) {
            return false;
        }
        return pixels.size() >= static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) *
                                  static_cast<std::size_t>(channels);
    }

    std::size_t pixelCount() const {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    void release() {
        width = 0;
        height = 0;
        channels = 0;
        pixels.clear();
        pixels.shrink_to_fit();
    }
};

}