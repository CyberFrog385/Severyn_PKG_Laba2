#include "core/codec/PixelAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace lab2 {

namespace {

constexpr std::size_t kColorCap = 65536;

std::uint32_t packRgb(int r, int g, int b) {
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(b);
}

int bitsUsed(std::uint8_t value) {
    int bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

}

PixelStats analyzeRgb(const ImageBuffer& buffer, const ImageRecord& record) {
    PixelStats stats;
    stats.width = buffer.width;
    stats.height = buffer.height;
    stats.pixelCount = static_cast<std::uint64_t>(buffer.pixelCount());
    if (!buffer.valid()) {
        stats.error = "буфер пикселей пуст";
        return stats;
    }

    const std::size_t pixelCount = buffer.pixelCount();
    const int channels = buffer.channels;
    stats.hasAlpha = channels == 2 || channels == 4;

    std::array<std::uint64_t, 256> histogram{};
    std::array<double, 3> sums{0.0, 0.0, 0.0};
    int minChannel[3] = {255, 255, 255};
    int maxChannel[3] = {0, 0, 0};
    int channelBits = 0;
    bool grayscale = true;
    bool alphaUsed = false;
    std::unordered_map<std::uint32_t, std::uint32_t> colorCounts;
    std::uint64_t colorOverflow = 0;

    const int colorChannels = channels == 2 ? 1 : 3;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::uint8_t* pixel = buffer.pixels.data() + i * static_cast<std::size_t>(channels);
        int component[3] = {pixel[0], pixel[0], pixel[0]};
        for (int c = 0; c < colorChannels && c < channels; ++c) {
            component[c] = pixel[c];
        }
        if (channels == 2) {
            component[1] = pixel[0];
            component[2] = pixel[0];
        }
        if (!(component[0] == component[1] && component[1] == component[2])) {
            grayscale = false;
        }
        for (int c = 0; c < 3; ++c) {
            sums[static_cast<std::size_t>(c)] += component[c];
            minChannel[c] = std::min(minChannel[c], component[c]);
            maxChannel[c] = std::max(maxChannel[c], component[c]);
            channelBits = std::max(channelBits, bitsUsed(static_cast<std::uint8_t>(component[c])));
        }
        ++histogram[static_cast<std::size_t>(pixel[0])];
        if (stats.hasAlpha && pixel[channels - 1] != 255) {
            alphaUsed = true;
        }
        const std::uint32_t color = packRgb(component[0], component[1], component[2]);
        auto slot = colorCounts.find(color);
        if (slot != colorCounts.end()) {
            ++slot->second;
        } else if (colorCounts.size() < kColorCap) {
            colorCounts.emplace(color, 1);
        } else {
            ++colorOverflow;
        }
    }

    stats.distinctColors = static_cast<std::uint32_t>(colorCounts.size());
    stats.distinctColorsCapped = colorOverflow > 0;
    stats.usedChannelBits = channelBits;
    stats.grayscale = grayscale;
    stats.alphaUsed = alphaUsed;
    for (int c = 0; c < 3; ++c) {
        stats.minChannel[c] = minChannel[c];
        stats.maxChannel[c] = maxChannel[c];
        stats.meanChannel[c] = pixelCount == 0 ? 0.0 : sums[static_cast<std::size_t>(c)] /
                                                            static_cast<double>(pixelCount);
    }

    for (int value = 1; value < 256; ++value) {
        const double probability = static_cast<double>(histogram[static_cast<std::size_t>(value)]) /
                                   static_cast<double>(pixelCount);
        if (probability > 0.0) {
            stats.entropy -= probability * std::log2(probability);
        }
    }

    std::vector<std::pair<std::uint32_t, std::uint32_t>> sorted;
    sorted.reserve(colorCounts.size());
    for (const auto& [color, count] : colorCounts) {
        sorted.emplace_back(color, count);
    }
    std::partial_sort(sorted.begin(), sorted.begin() + std::min<std::size_t>(8, sorted.size()),
                      sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [color, count] : sorted) {
        stats.topColors.emplace_back(color, 100.0 * static_cast<double>(count) /
                                               static_cast<double>(pixelCount));
        if (stats.topColors.size() >= 8) {
            break;
        }
    }

    const std::uint64_t decodedBytes =
        static_cast<std::uint64_t>(buffer.pixels.size() + pixelCount);
    if (record.fileSize > 0) {
        stats.compressionRatio = static_cast<double>(record.fileSize) /
                                 static_cast<double>(std::max<std::uint64_t>(decodedBytes, 1));
    }
    stats.decoded = true;
    return stats;
}

}