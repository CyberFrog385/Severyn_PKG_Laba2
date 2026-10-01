#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace lab2 {

struct PixelStats {
    bool decoded = false;
    std::string error;
    std::string decoder;

    int width = 0;
    int height = 0;
    std::uint64_t pixelCount = 0;

    std::uint32_t distinctColors = 0;
    bool distinctColorsCapped = false;
    int usedChannelBits = 0;
    bool grayscale = false;
    bool hasAlpha = false;
    bool alphaUsed = false;

    double minChannel[3] = {0.0, 0.0, 0.0};
    double maxChannel[3] = {0.0, 0.0, 0.0};
    double meanChannel[3] = {0.0, 0.0, 0.0};

    double entropy = 0.0;
    double compressionRatio = 0.0;
    std::vector<std::pair<std::uint32_t, double>> topColors;
};

}