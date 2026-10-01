#include "core/codec/BmpDecoder.h"

#include <algorithm>

#include "core/ByteReader.h"
#include "core/codec/PixelAnalyzer.h"
#include "core/codec/RleCodec.h"

namespace lab2 {

namespace {

int countShift(int bitCount) {
    int shift = 0;
    while (bitCount > 1) {
        bitCount >>= 1;
        ++shift;
    }
    return shift;
}

int extractChannel(std::uint32_t value, std::uint32_t mask) {
    if (mask == 0) {
        return 0;
    }
    int shift = 0;
    while (((mask >> shift) & 1u) == 0u) {
        ++shift;
    }
    const std::uint32_t widthMask = mask >> shift;
    const int width = countShift(static_cast<int>(widthMask)) + 1;
    const int maximum = (1 << width) - 1;
    const std::uint32_t raw = (value & mask) >> shift;
    return maximum == 0 ? 0 : static_cast<int>((raw * 255u) / static_cast<std::uint32_t>(maximum));
}

std::size_t sampleAt(const std::uint8_t* row, std::size_t index, int bitCount) {
    switch (bitCount) {
        case 1:
            return (row[index / 8] >> (7 - (index % 8))) & 1u;
        case 2:
            return (row[index / 4] >> (6 - 2 * (index % 4))) & 3u;
        case 4:
            return (row[index / 2] >> (4 - 4 * (index % 2))) & 0x0Fu;
        case 8:
            return row[index];
        case 16: {
            const std::uint16_t value =
                static_cast<std::uint16_t>(row[index * 2] | (row[index * 2 + 1] << 8));
            return value;
        }
        default:
            return 0;
    }
}

}

DecodeResult decodeBmp(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    DecodeResult result;
    result.decoder = "BMP (палитра, RLE4/RLE8, маски каналов)";

    std::vector<std::uint8_t> head;
    if (!file.readAt(0, 14, head)) {
        result.error = "файл слишком мал для BMP";
        return result;
    }
    ByteReader reader(head.data(), head.size());
    reader.skip(2);
    reader.u32le();
    reader.skip(4);
    const std::uint32_t pixelOffset = reader.u32le();

    std::vector<std::uint8_t> dibBytes;
    if (!file.readAt(14, 128, dibBytes)) {
        result.error = "не удалось прочитать DIB-заголовок";
        return result;
    }
    ByteReader dib(dibBytes.data(), dibBytes.size());
    const std::uint32_t headerSize = dib.u32le();
    int width = 0;
    int height = 0;
    std::uint16_t planes = 0;
    std::uint16_t bitCount = 0;
    std::uint32_t compression = 0;
    std::uint32_t paletteColors = 0;

    if (headerSize == 12) {
        width = dib.u16le();
        height = dib.u16le();
        planes = dib.u16le();
        bitCount = dib.u16le();
    } else if (headerSize >= 40) {
        width = static_cast<int>(dib.u32le());
        height = static_cast<int>(dib.u32le());
        planes = dib.u16le();
        bitCount = dib.u16le();
        compression = dib.u32le();
        dib.skip(12);
        paletteColors = dib.u32le();
    } else {
        result.error = "неизвестный размер DIB-заголовка";
        return result;
    }
    const bool topDown = height < 0;
    const int absoluteHeight = std::abs(height);

    if (width <= 0 || absoluteHeight <= 0) {
        result.error = "нулевой размер изображения";
        return result;
    }
    if (planes != 1) {
        result.error = "число плоскостей не равно 1";
        return result;
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(absoluteHeight);
    if (pixelCount > limits.maxPixels) {
        result.error = "изображение превышает ограничение по числу пикселей";
        result.limited = true;
        return result;
    }

    std::uint32_t redMask = 0;
    std::uint32_t greenMask = 0;
    std::uint32_t blueMask = 0;
    if (bitCount == 16 && compression == 3) {
        if (!file.readAt(14 + 40, 12, head)) {
            result.error = "не удалось прочитать маски каналов";
            return result;
        }
        ByteReader masks(head.data(), head.size());
        redMask = masks.u32le();
        greenMask = masks.u32le();
        blueMask = masks.u32le();
    } else if (bitCount == 16 && compression == 0) {
        redMask = 0x7C00;
        greenMask = 0x03E0;
        blueMask = 0x001F;
    }

    std::vector<std::uint8_t> paletteRgb;
    if (bitCount <= 8) {
        std::uint32_t entries = paletteColors;
        if (entries == 0) {
            entries = 1u << bitCount;
        }
        if (entries > 256) {
            entries = 256;
        }
        const std::uint64_t paletteOffset =
            static_cast<std::uint64_t>(14) + headerSize;
        paletteRgb.resize(static_cast<std::size_t>(entries) * 3);
        for (std::uint32_t i = 0; i < entries; ++i) {
            std::uint8_t entry[4] = {};
            if (!file.readInto(paletteOffset + i * 4, 4, entry)) {
                paletteRgb.resize(static_cast<std::size_t>(i) * 3);
                break;
            }
            paletteRgb[static_cast<std::size_t>(i) * 3] = entry[2];
            paletteRgb[static_cast<std::size_t>(i) * 3 + 1] = entry[1];
            paletteRgb[static_cast<std::size_t>(i) * 3 + 2] = entry[0];
        }
    }

    result.buffer.width = width;
    result.buffer.height = absoluteHeight;
    result.buffer.channels = 3;
    result.buffer.pixels.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(absoluteHeight) * 3, 0);

    const std::size_t rowSize =
        ((static_cast<std::size_t>(width) * bitCount + 31) / 32) * 4;
    std::vector<std::uint8_t> raw;

    if (compression == 1 || compression == 2) {
        const std::uint64_t available = file.size() > pixelOffset ? file.size() - pixelOffset : 0;
        const std::size_t limit =
            static_cast<std::size_t>(std::min<std::uint64_t>(available, limits.maxCompressedBytes));
        raw.resize(limit);
        if (limit > 0 && !file.readInto(pixelOffset, limit, raw.data())) {
            result.error = "не удалось прочитать сжатые данные";
            return result;
        }
        std::vector<std::uint8_t> indices;
        const RleResult rle =
            compression == 1 ? decodeBmpRle8(raw.data(), raw.size(), width, absoluteHeight, indices)
                             : decodeBmpRle4(raw.data(), raw.size(), width, absoluteHeight, indices);
        if (!rle.ok) {
            result.error = rle.error;
            return result;
        }
        for (std::uint64_t i = 0; i < pixelCount; ++i) {
            const std::uint8_t index =
                i < indices.size() ? indices[static_cast<std::size_t>(i)] : 0;
            const std::size_t colorIndex = index < paletteRgb.size() / 3 ? index : 0;
            const int x = static_cast<int>(i % static_cast<std::uint64_t>(width));
            const int y = static_cast<int>(i / static_cast<std::uint64_t>(width));
            const int targetY = topDown ? y : absoluteHeight - 1 - y;
            std::uint8_t* target =
                result.buffer.pixels.data() +
                (static_cast<std::size_t>(targetY) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 3;
            target[0] = paletteRgb[colorIndex * 3];
            target[1] = paletteRgb[colorIndex * 3 + 1];
            target[2] = paletteRgb[colorIndex * 3 + 2];
        }
    } else {
        if (compression != 0 && compression != 3) {
            result.error = "неподдерживаемый метод сжатия BMP";
            result.unsupported = true;
            return result;
        }
        if (static_cast<std::uint64_t>(rowSize) * static_cast<std::uint64_t>(absoluteHeight) >
            limits.maxDecodedBytes) {
            result.error = "распакованный размер превышает допустимый предел";
            return result;
        }
        for (int y = 0; y < absoluteHeight; ++y) {
            const std::uint64_t rowOffset =
                pixelOffset + static_cast<std::uint64_t>(y) * rowSize;
            std::vector<std::uint8_t> row(rowSize);
            if (rowSize > 0 && !file.readInto(rowOffset, rowSize, row.data())) {
                result.error = "файл закончился в данных изображения";
                return result;
            }
            const int targetY = topDown ? y : absoluteHeight - 1 - y;
            for (int x = 0; x < width; ++x) {
                std::uint8_t* target =
                    result.buffer.pixels.data() +
                    (static_cast<std::size_t>(targetY) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) * 3;
                if (bitCount <= 8) {
                    const std::size_t index = sampleAt(row.data(), static_cast<std::size_t>(x), bitCount);
                    const std::size_t colorIndex = index < paletteRgb.size() / 3 ? index : 0;
                    target[0] = paletteRgb[colorIndex * 3];
                    target[1] = paletteRgb[colorIndex * 3 + 1];
                    target[2] = paletteRgb[colorIndex * 3 + 2];
                } else if (bitCount == 24) {
                    target[0] = row[static_cast<std::size_t>(x) * 3 + 2];
                    target[1] = row[static_cast<std::size_t>(x) * 3 + 1];
                    target[2] = row[static_cast<std::size_t>(x) * 3];
                } else if (bitCount == 32) {
                    target[0] = row[static_cast<std::size_t>(x) * 4 + 2];
                    target[1] = row[static_cast<std::size_t>(x) * 4 + 1];
                    target[2] = row[static_cast<std::size_t>(x) * 4];
                } else if (bitCount == 16) {
                    const std::uint32_t value = static_cast<std::uint32_t>(sampleAt(
                        row.data(), static_cast<std::size_t>(x), 16));
                    target[0] = static_cast<std::uint8_t>(extractChannel(value, redMask));
                    target[1] = static_cast<std::uint8_t>(extractChannel(value, greenMask));
                    target[2] = static_cast<std::uint8_t>(extractChannel(value, blueMask));
                } else {
                    result.error = "неподдерживаемая глубина BMP";
                    result.unsupported = true;
                    return result;
                }
            }
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