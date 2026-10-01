#include "core/codec/GifDecoder.h"

#include <algorithm>
#include <cstring>

#include "core/ByteReader.h"
#include "core/codec/Lzw.h"
#include "core/codec/PixelAnalyzer.h"

namespace lab2 {

namespace {

struct GifPalette {
    std::vector<std::uint8_t> rgb;
    std::size_t count() const { return rgb.size() / 3; }
};

void deinterlace(const std::vector<std::uint8_t>& indices, int width, int height,
                 std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    const int starts[4] = {0, 4, 2, 1};
    const int steps[4] = {8, 8, 4, 2};
    std::size_t source = 0;
    for (int pass = 0; pass < 4; ++pass) {
        for (int y = starts[pass]; y < height; y += steps[pass]) {
            for (int x = 0; x < width; ++x) {
                if (source >= indices.size()) {
                    return;
                }
                out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)] = indices[source++];
            }
        }
    }
}

bool collectSubBlocks(FileHead& file, std::uint64_t& position, std::size_t limit,
                      std::vector<std::uint8_t>& out, std::string& error) {
    while (true) {
        std::uint8_t sizeByte = 0;
        if (!file.readInto(position, 1, &sizeByte)) {
            error = "файл закончился внутри блока данных";
            return false;
        }
        ++position;
        if (sizeByte == 0) {
            return true;
        }
        if (out.size() + sizeByte > limit) {
            error = "размер блока данных превышает допустимый предел";
            return false;
        }
        const std::size_t base = out.size();
        out.resize(base + sizeByte);
        if (!file.readInto(position, sizeByte, out.data() + base)) {
            error = "не удалось прочитать блок данных";
            return false;
        }
        position += sizeByte;
    }
}

}

DecodeResult decodeGif(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    DecodeResult result;
    result.decoder = "GIF (LZW, палитра, чередование строк)";

    std::vector<std::uint8_t> head;
    if (!file.readAt(0, 13, head)) {
        result.error = "файл слишком мал для GIF";
        return result;
    }
    ByteReader reader(head.data(), head.size());
    const std::string signature = reader.rawString(6);
    const int screenWidth = reader.u16le();
    const int screenHeight = reader.u16le();
    const std::uint8_t packed = reader.u8();
    reader.skip(3);
    if (signature != "GIF87a" && signature != "GIF89a") {
        result.error = "сигнатура GIF не распознана";
        return result;
    }

    std::uint64_t position = 13;
    GifPalette globalPalette;
    if ((packed & 0x80) != 0) {
        const std::size_t entries = static_cast<std::size_t>(2) << (packed & 0x07);
        globalPalette.rgb.resize(entries * 3);
        if (!file.readInto(position, entries * 3, globalPalette.rgb.data())) {
            result.error = "не удалось прочитать глобальную палитру";
            return result;
        }
        position += entries * 3;
    }

    const std::uint64_t pixelLimit =
        std::min<std::uint64_t>(limits.maxPixels, limits.maxDecodedBytes / 4);

    while (position < file.size()) {
        std::uint8_t marker = 0;
        if (!file.readInto(position, 1, &marker)) {
            break;
        }
        ++position;
        if (marker == 0x3B) {
            result.error = "кадр изображения не найден";
            return result;
        }
        if (marker == 0x21) {
            std::uint8_t label = 0;
            if (!file.readInto(position, 1, &label)) {
                break;
            }
            ++position;
            if (label == 0xF9 || label == 0xFE || label == 0x01) {
                std::vector<std::uint8_t> block;
                std::string error;
                if (!collectSubBlocks(file, position, limits.maxCompressedBytes, block, error)) {
                    result.error = error;
                    return result;
                }
            } else if (label == 0xFF) {
                std::uint8_t blockSize = 0;
                if (!file.readInto(position, 1, &blockSize)) {
                    break;
                }
                ++position;
                position += blockSize;
                std::vector<std::uint8_t> block;
                std::string error;
                if (!collectSubBlocks(file, position, limits.maxCompressedBytes, block, error)) {
                    result.error = error;
                    return result;
                }
            } else {
                std::vector<std::uint8_t> block;
                std::string error;
                if (!collectSubBlocks(file, position, limits.maxCompressedBytes, block, error)) {
                    result.error = error;
                    return result;
                }
            }
            continue;
        }
        if (marker != 0x2C) {
            result.error = "неожиданный маркер блока";
            return result;
        }

        std::uint8_t descriptor[9] = {};
        if (!file.readInto(position, 9, descriptor)) {
            result.error = "файл закончился в дескрипторе кадра";
            return result;
        }
        ByteReader frame(descriptor, sizeof(descriptor));
        const int left = frame.u16le();
        const int top = frame.u16le();
        const int width = frame.u16le();
        const int height = frame.u16le();
        const std::uint8_t framePacked = frame.u8();
        position += 9;

        GifPalette palette = globalPalette;
        if ((framePacked & 0x80) != 0) {
            const std::size_t entries = static_cast<std::size_t>(2) << (framePacked & 0x07);
            palette.rgb.resize(entries * 3);
            if (!file.readInto(position, entries * 3, palette.rgb.data())) {
                result.error = "не удалось прочитать локальную палитру";
                return result;
            }
            position += entries * 3;
        }

        std::uint8_t minimumCodeSize = 0;
        if (!file.readInto(position, 1, &minimumCodeSize)) {
            result.error = "файл закончился перед данными кадра";
            return result;
        }
        ++position;
        std::vector<std::uint8_t> compressed;
        std::string blockError;
        if (!collectSubBlocks(file, position, limits.maxCompressedBytes, compressed, blockError)) {
            result.error = blockError;
            return result;
        }

        if (width <= 0 || height <= 0) {
            result.error = "нулевой размер кадра";
            return result;
        }
        const std::uint64_t framePixels =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (framePixels > pixelLimit) {
            result.error = "кадр превышает допустимое число пикселей";
            return result;
        }

        std::vector<std::uint8_t> indices;
        const LzwResult lzw =
            lzwDecodeGif(compressed.data(), compressed.size(), minimumCodeSize,
                         static_cast<std::size_t>(framePixels), indices);
        if (!lzw.ok) {
            result.error = lzw.error;
            return result;
        }
        if (indices.size() < framePixels) {
            result.error = "распаковано меньше пикселей, чем заявлено размером кадра";
            return result;
        }

        std::vector<std::uint8_t> pixels;
        if ((framePacked & 0x40) != 0) {
            deinterlace(indices, width, height, pixels);
        } else {
            pixels = indices;
        }

        const int canvasWidth = screenWidth > 0 ? screenWidth : width;
        const int canvasHeight = screenHeight > 0 ? screenHeight : height;
        result.buffer.width = canvasWidth;
        result.buffer.height = canvasHeight;
        result.buffer.channels = 3;
        result.buffer.pixels.assign(
            static_cast<std::size_t>(canvasWidth) * static_cast<std::size_t>(canvasHeight) * 3,
            0);
        const std::size_t paletteSize = palette.count();
        if (paletteSize == 0) {
            result.error = "у кадра отсутствует палитра";
            return result;
        }
        for (int y = 0; y < height; ++y) {
            const int targetY = top + y;
            if (targetY < 0 || targetY >= canvasHeight) {
                continue;
            }
            for (int x = 0; x < width; ++x) {
                const int targetX = left + x;
                if (targetX < 0 || targetX >= canvasWidth) {
                    continue;
                }
                const std::uint8_t index =
                    pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)];
                const std::size_t safeIndex = index < paletteSize ? index : 0;
                std::uint8_t* target =
                    result.buffer.pixels.data() +
                    ((static_cast<std::size_t>(targetY) * static_cast<std::size_t>(canvasWidth)) +
                     static_cast<std::size_t>(targetX)) * 3;
                target[0] = palette.rgb[safeIndex * 3];
                target[1] = palette.rgb[safeIndex * 3 + 1];
                target[2] = palette.rgb[safeIndex * 3 + 2];
            }
        }

        result.stats = analyzeRgb(result.buffer, record);
        result.ok = result.buffer.valid();
        if (!result.ok) {
            result.error = "не удалось собрать изображение целиком";
        }
        return result;
    }

    result.error = "кадр изображения не найден";
    return result;
}

}