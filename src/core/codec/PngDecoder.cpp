#include "core/codec/PngDecoder.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/ByteReader.h"
#include "core/codec/Inflate.h"
#include "core/codec/PixelAnalyzer.h"

namespace lab2 {

namespace {

constexpr std::uint32_t kAdam7XStart[7] = {0, 4, 0, 2, 0, 1, 0};
constexpr std::uint32_t kAdam7YStart[7] = {0, 0, 4, 0, 2, 0, 1};
constexpr std::uint32_t kAdam7XStep[7] = {8, 8, 4, 4, 2, 2, 1};
constexpr std::uint32_t kAdam7YStep[7] = {8, 8, 8, 4, 4, 2, 2};

struct PngHeader {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bitDepth = 0;
    std::uint8_t colorType = 0;
    std::uint8_t compression = 0;
    std::uint8_t filterMethod = 0;
    std::uint8_t interlace = 0;
};

int samplesPerPixel(std::uint8_t colorType) {
    switch (colorType) {
        case 0:
        case 3:
            return 1;
        case 2:
            return 3;
        case 4:
            return 2;
        case 6:
            return 4;
        default:
            return 0;
    }
}

bool validBitDepth(std::uint8_t colorType, std::uint8_t bitDepth) {
    switch (colorType) {
        case 0:
            return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
        case 3:
            return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
        case 2:
        case 4:
        case 6:
            return bitDepth == 8 || bitDepth == 16;
        default:
            return false;
    }
}

int sampleAt(const std::uint8_t* row, std::size_t index, int bitDepth) {
    if (bitDepth == 8) {
        return row[index];
    }
    if (bitDepth == 16) {
        return row[index * 2];
    }
    const int perByte = 8 / bitDepth;
    const int shift = 8 - bitDepth * (static_cast<int>(index % static_cast<std::size_t>(perByte)) + 1);
    return (row[index / static_cast<std::size_t>(perByte)] >> shift) & ((1 << bitDepth) - 1);
}

int scaleToEight(int value, int bitDepth) {
    if (bitDepth >= 8) {
        return value;
    }
    return value * 255 / ((1 << bitDepth) - 1);
}

int paethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

bool unfilterRow(std::uint8_t filter, std::uint8_t* row, const std::uint8_t* previous,
                 std::size_t rowBytes, int bytesPerPixel) {
    const int bpp = std::max(bytesPerPixel, 1);
    for (std::size_t i = 0; i < rowBytes; ++i) {
        const int left = i >= static_cast<std::size_t>(bpp) ? row[i - static_cast<std::size_t>(bpp)] : 0;
        const int up = previous != nullptr ? previous[i] : 0;
        const int upLeft =
            previous != nullptr && i >= static_cast<std::size_t>(bpp)
                ? previous[i - static_cast<std::size_t>(bpp)]
                : 0;
        switch (filter) {
            case 0:
                break;
            case 1:
                row[i] = static_cast<std::uint8_t>(row[i] + left);
                break;
            case 2:
                row[i] = static_cast<std::uint8_t>(row[i] + up);
                break;
            case 3:
                row[i] = static_cast<std::uint8_t>(row[i] + ((left + up) / 2));
                break;
            case 4:
                row[i] = static_cast<std::uint8_t>(row[i] + paethPredictor(left, up, upLeft));
                break;
            default:
                return false;
        }
    }
    return true;
}

void emitPixel(ImageBuffer& out, std::size_t target, const PngHeader& header,
               const std::uint8_t* row, std::size_t pixelIndex,
               const std::vector<std::uint8_t>& palette, const std::vector<std::uint8_t>& paletteAlpha,
               const std::vector<int>& keySamples) {
    const std::size_t stride = static_cast<std::size_t>(out.channels);
    std::uint8_t* pixel = out.pixels.data() + target * stride;
    int r = 0;
    int g = 0;
    int b = 0;
    int alpha = 255;
    if (header.colorType == 0) {
        const int gray = scaleToEight(sampleAt(row, pixelIndex, header.bitDepth), header.bitDepth);
        r = gray;
        g = gray;
        b = gray;
    } else if (header.colorType == 2) {
        r = scaleToEight(sampleAt(row, pixelIndex * 3, header.bitDepth), header.bitDepth);
        g = scaleToEight(sampleAt(row, pixelIndex * 3 + 1, header.bitDepth), header.bitDepth);
        b = scaleToEight(sampleAt(row, pixelIndex * 3 + 2, header.bitDepth), header.bitDepth);
    } else if (header.colorType == 3) {
        const int index = sampleAt(row, pixelIndex, header.bitDepth);
        if (static_cast<std::size_t>(index) * 3 + 2 < palette.size()) {
            r = palette[static_cast<std::size_t>(index) * 3];
            g = palette[static_cast<std::size_t>(index) * 3 + 1];
            b = palette[static_cast<std::size_t>(index) * 3 + 2];
        }
        if (static_cast<std::size_t>(index) < paletteAlpha.size()) {
            alpha = paletteAlpha[static_cast<std::size_t>(index)];
        }
    } else if (header.colorType == 4) {
        const int gray = scaleToEight(sampleAt(row, pixelIndex * 2, header.bitDepth), header.bitDepth);
        r = gray;
        g = gray;
        b = gray;
        alpha = scaleToEight(sampleAt(row, pixelIndex * 2 + 1, header.bitDepth), header.bitDepth);
    } else {
        r = scaleToEight(sampleAt(row, pixelIndex * 4, header.bitDepth), header.bitDepth);
        g = scaleToEight(sampleAt(row, pixelIndex * 4 + 1, header.bitDepth), header.bitDepth);
        b = scaleToEight(sampleAt(row, pixelIndex * 4 + 2, header.bitDepth), header.bitDepth);
        alpha = scaleToEight(sampleAt(row, pixelIndex * 4 + 3, header.bitDepth), header.bitDepth);
    }
    if (!keySamples.empty()) {
        const std::size_t count = keySamples.size();
        bool match = true;
        for (std::size_t c = 0; c < count; ++c) {
            const std::size_t index = header.colorType == 0 ? 0 : c;
            if (sampleAt(row, pixelIndex * count + index, header.bitDepth) != keySamples[c]) {
                match = false;
                break;
            }
        }
        if (match) {
            alpha = 0;
        }
    }
    pixel[0] = static_cast<std::uint8_t>(r);
    pixel[1] = static_cast<std::uint8_t>(g);
    pixel[2] = static_cast<std::uint8_t>(b);
    if (stride == 4) {
        pixel[3] = static_cast<std::uint8_t>(alpha);
    }
}

}

DecodeResult decodePng(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    DecodeResult result;
    result.decoder = "PNG (DEFLATE, фильтры строк, Adam7)";

    std::uint8_t headerBytes[33] = {};
    if (!file.readInto(0, 33, headerBytes)) {
        result.error = "не удалось прочитать заголовок PNG";
        return result;
    }
    ByteReader header(headerBytes, 33);
    header.skip(8);
    const std::uint32_t length = header.u32be();
    const std::string type = header.rawString(4);
    if (length != 13 || type != "IHDR") {
        result.error = "чанк IHDR отсутствует или имеет неверную длину";
        return result;
    }
    PngHeader png;
    png.width = header.u32be();
    png.height = header.u32be();
    png.bitDepth = header.u8();
    png.colorType = header.u8();
    png.compression = header.u8();
    png.filterMethod = header.u8();
    png.interlace = header.u8();

    if (png.width == 0 || png.height == 0) {
        result.error = "нулевой размер изображения";
        return result;
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(png.width) * static_cast<std::uint64_t>(png.height);
    if (pixelCount > limits.maxPixels) {
        result.error = "изображение превышает ограничение по числу пикселей";
        result.limited = true;
        return result;
    }
    if (samplesPerPixel(png.colorType) == 0 || !validBitDepth(png.colorType, png.bitDepth)) {
        result.error = "неподдерживаемая комбинация типа цвета и глубины";
        result.unsupported = true;
        return result;
    }

    const std::uint64_t compressedLimit = std::min<std::uint64_t>(
        limits.maxCompressedBytes, std::max<std::uint64_t>(record.fileSize, 1));
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(compressedLimit, 512u * 1024u * 1024u)));
    std::vector<std::uint8_t> compressed;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> paletteAlpha;
    std::vector<int> keySamples;

    const std::uint32_t totalWidth = png.width;
    const std::uint32_t totalHeight = png.height;
    std::uint64_t position = 8;
    while (true) {
        std::uint8_t chunkHeader[8] = {};
        if (!file.readInto(position, 8, chunkHeader)) {
            result.error = "файл закончился до чанка IEND";
            return result;
        }
        ByteReader chunk(chunkHeader, 8);
        const std::uint32_t dataLength = chunk.u32be();
        const std::string name = chunk.rawString(4);
        const std::uint64_t dataStart = position + 8;
        if (name == "IEND") {
            break;
        }
        if (name == "IDAT") {
            if (compressed.size() + dataLength > limits.maxCompressedBytes) {
                result.error = "поток IDAT превышает допустимый размер";
                return result;
            }
            const std::size_t base = compressed.size();
            const std::size_t available = static_cast<std::size_t>(
                std::min<std::uint64_t>(dataLength, file.size() > dataStart ? file.size() - dataStart : 0));
            compressed.resize(base + available);
            if (available > 0 && !file.readInto(dataStart, available, compressed.data() + base)) {
                result.error = "не удалось прочитать данные IDAT";
                return result;
            }
        } else if (name == "PLTE") {
            palette.resize(dataLength);
            if (dataLength > 0 && !file.readInto(dataStart, dataLength, palette.data())) {
                result.error = "не удалось прочитать палитру PLTE";
                return result;
            }
        } else if (name == "tRNS" && png.colorType == 3) {
            paletteAlpha.resize(dataLength);
            if (dataLength > 0 && !file.readInto(dataStart, dataLength, paletteAlpha.data())) {
                result.error = "не удалось прочитать прозрачность tRNS";
                return result;
            }
        } else if (name == "tRNS" && (png.colorType == 0 || png.colorType == 2)) {
            const std::size_t needed = png.colorType == 0 ? 2u : 6u;
            std::vector<std::uint8_t> key;
            if (dataLength != needed || !file.readAt(dataStart, needed, key) || key.size() != needed) {
                result.error = "чанк tRNS имеет неверную длину для типа цвета " +
                               std::to_string(png.colorType);
                return result;
            }
            for (std::size_t i = 0; i + 1 < key.size(); i += 2) {
                keySamples.push_back(static_cast<int>((key[i] << 8) | key[i + 1]));
            }
        }
        position = dataStart + dataLength + 4;
        if (position >= file.size()) {
            result.error = "чанк IEND не найден";
            return result;
        }
    }

    if (compressed.empty()) {
        result.error = "данные изображения (IDAT) отсутствуют";
        return result;
    }

    const int bpp = std::max(1, samplesPerPixel(png.colorType) * png.bitDepth / 8);
    std::size_t maxRaw = 0;
    for (int pass = 0; pass < (png.interlace == 1 ? 7 : 1); ++pass) {
        const std::uint32_t xs = png.interlace == 1 ? kAdam7XStart[static_cast<std::size_t>(pass)] : 0;
        const std::uint32_t ys = png.interlace == 1 ? kAdam7YStart[static_cast<std::size_t>(pass)] : 0;
        const std::uint32_t dx = png.interlace == 1 ? kAdam7XStep[static_cast<std::size_t>(pass)] : 1;
        const std::uint32_t dy = png.interlace == 1 ? kAdam7YStep[static_cast<std::size_t>(pass)] : 1;
        if (totalWidth <= xs || totalHeight <= ys) {
            continue;
        }
        const std::uint32_t passWidth = (totalWidth - xs + dx - 1) / dx;
        const std::uint32_t passHeight = (totalHeight - ys + dy - 1) / dy;
        maxRaw += static_cast<std::size_t>(passHeight) *
                  (1 + (static_cast<std::size_t>(passWidth) * static_cast<std::size_t>(samplesPerPixel(png.colorType)) *
                               static_cast<std::size_t>(png.bitDepth) + 7) / 8);
    }
    maxRaw = std::min<std::size_t>(maxRaw + 64, limits.maxDecodedBytes);

    const InflateResult inflated = zlibInflate(compressed.data(), compressed.size(), raw, maxRaw);
    if (!inflated.ok) {
        result.error = inflated.error;
        return result;
    }

    const bool hasAlpha =
        png.colorType == 4 || png.colorType == 6 || !paletteAlpha.empty() || !keySamples.empty();
    result.buffer.width = static_cast<int>(totalWidth);
    result.buffer.height = static_cast<int>(totalHeight);
    result.buffer.channels = hasAlpha ? 4 : 3;
    result.buffer.pixels.assign(static_cast<std::size_t>(totalWidth) * totalHeight *
                                    static_cast<std::size_t>(result.buffer.channels),
                                0);

    const int sampleCount = samplesPerPixel(png.colorType);
    std::size_t rawPosition = 0;
    const int passes = png.interlace == 1 ? 7 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        const std::uint32_t xs = png.interlace == 1 ? kAdam7XStart[static_cast<std::size_t>(pass)] : 0;
        const std::uint32_t ys = png.interlace == 1 ? kAdam7YStart[static_cast<std::size_t>(pass)] : 0;
        const std::uint32_t dx = png.interlace == 1 ? kAdam7XStep[static_cast<std::size_t>(pass)] : 1;
        const std::uint32_t dy = png.interlace == 1 ? kAdam7YStep[static_cast<std::size_t>(pass)] : 1;
        if (totalWidth <= xs || totalHeight <= ys) {
            continue;
        }
        const std::uint32_t passWidth = (totalWidth - xs + dx - 1) / dx;
        const std::uint32_t passHeight = (totalHeight - ys + dy - 1) / dy;
        const std::size_t rowBytes =
            (static_cast<std::size_t>(passWidth) * static_cast<std::size_t>(sampleCount) *
                 static_cast<std::size_t>(png.bitDepth) +
             7) / 8;
        std::vector<std::uint8_t> current(rowBytes, 0);
        std::vector<std::uint8_t> previous(rowBytes, 0);
        for (std::uint32_t y = 0; y < passHeight; ++y) {
            if (rawPosition + 1 + rowBytes > raw.size()) {
                result.error = "распакованные данные обрезаны";
                return result;
            }
            const std::uint8_t filter = raw[rawPosition++];
            std::memcpy(current.data(), raw.data() + rawPosition, rowBytes);
            rawPosition += rowBytes;
            if (!unfilterRow(filter, current.data(), y == 0 ? nullptr : previous.data(), rowBytes, bpp)) {
                result.error = "неизвестный метод фильтрации строки";
                return result;
            }
            for (std::uint32_t x = 0; x < passWidth; ++x) {
                const std::size_t target =
                    static_cast<std::size_t>(ys + y * dy) * totalWidth + (xs + x * dx);
                emitPixel(result.buffer, target, png, current.data(), x, palette, paletteAlpha,
                          keySamples);
            }
            previous.swap(current);
        }
    }

    result.stats = analyzeRgb(result.buffer, record);
    result.ok = result.buffer.valid();
    if (!result.ok) {
        result.error = "не удалось собрать изображение целиком";
    }
    return result;
}

}