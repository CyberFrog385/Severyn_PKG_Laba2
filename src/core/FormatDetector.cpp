#include "core/FormatDetector.h"

#include <cstdio>
#include <cstring>

namespace lab2 {

namespace {

bool match(const std::uint8_t* data, std::size_t size, std::size_t count, const std::uint8_t* pattern) {
    return size >= count && std::memcmp(data, pattern, count) == 0;
}

std::string signatureText(const std::uint8_t* data, std::size_t count) {
    std::string text;
    char buffer[8];
    for (std::size_t i = 0; i < count && i < 8; ++i) {
        std::snprintf(buffer, sizeof(buffer), "%02X ", data[i]);
        text += buffer;
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return text;
}

}

Detection detectFormat(const std::uint8_t* data, std::size_t size) {
    static const std::uint8_t pngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    static const std::uint8_t bm[2] = {'B', 'M'};
    static const std::uint8_t gif87[6] = {'G', 'I', 'F', '8', '7', 'a'};
    static const std::uint8_t gif89[6] = {'G', 'I', 'F', '8', '9', 'a'};
    static const std::uint8_t tiffLe[4] = {'I', 'I', 0x2A, 0x00};
    static const std::uint8_t tiffBe[4] = {'M', 'M', 0x00, 0x2A};
    static const std::uint8_t bigTiffLe[4] = {'I', 'I', 0x2B, 0x00};
    static const std::uint8_t bigTiffBe[4] = {'M', 'M', 0x00, 0x2B};

    Detection detection;
    if (match(data, size, 8, pngSignature)) {
        detection.format = Format::Png;
        detection.signature = signatureText(data, 8);
        return detection;
    }
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        detection.format = Format::Jpeg;
        detection.signature = signatureText(data, 3);
        return detection;
    }
    if (match(data, size, 6, gif87) || match(data, size, 6, gif89)) {
        detection.format = Format::Gif;
        detection.signature = signatureText(data, 6);
        return detection;
    }
    if (match(data, size, 4, tiffLe) || match(data, size, 4, tiffBe) ||
        match(data, size, 4, bigTiffLe) || match(data, size, 4, bigTiffBe)) {
        detection.format = Format::Tiff;
        detection.signature = signatureText(data, 4);
        return detection;
    }
    if (match(data, size, 2, bm)) {
        detection.format = Format::Bmp;
        detection.signature = signatureText(data, 2);
        return detection;
    }
    if (size >= 4 && data[0] == 0x0A && data[2] <= 1 &&
        (data[1] == 0 || data[1] == 2 || data[1] == 3 || data[1] == 4 || data[1] == 5) &&
        (data[3] == 1 || data[3] == 2 || data[3] == 4 || data[3] == 8)) {
        detection.format = Format::Pcx;
        detection.signature = signatureText(data, 4);
        return detection;
    }
    detection.format = Format::NotImage;
    return detection;
}

}