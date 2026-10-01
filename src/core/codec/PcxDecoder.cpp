#include "core/codec/PcxDecoder.h"

#include <algorithm>

#include "core/ByteReader.h"
#include "core/codec/PixelAnalyzer.h"
#include "core/codec/RleCodec.h"

namespace lab2 {

DecodeResult decodePcx(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    DecodeResult result;
    result.decoder = "PCX (RLE, плоскости, палитра EGA/VGA)";

    std::vector<std::uint8_t> head;
    if (!file.readAt(0, 128, head)) {
        result.error = "файл слишком мал для PCX";
        return result;
    }
    ByteReader reader(head.data(), head.size());
    reader.skip(1);
    const std::uint8_t version = reader.u8();
    const std::uint8_t encoding = reader.u8();
    const std::uint8_t bitsPerPixel = reader.u8();
    const int xMin = reader.u16le();
    const int yMin = reader.u16le();
    const int xMax = reader.u16le();
    const int yMax = reader.u16le();
    reader.skip(4);
    reader.skip(48);
    reader.skip(1);
    const std::uint8_t planes = reader.u8();
    const int bytesPerLine = reader.u16le();
    const std::uint16_t paletteInfo = reader.u16le();

    if (version != 5) {
        result.error = "поддерживается только версия PCX 5";
        return result;
    }
    if (encoding != 1) {
        result.error = "поддерживается только RLE-кодирование";
        return result;
    }
    if (planes != 1 && planes != 3 && planes != 4) {
        result.error = "неподдерживаемое число плоскостей";
        result.unsupported = true;
        return result;
    }
    if (bitsPerPixel != 1 && bitsPerPixel != 2 && bitsPerPixel != 4 && bitsPerPixel != 8) {
        result.error = "неподдерживаемая глубина PCX";
        result.unsupported = true;
        return result;
    }

    const int width = xMax - xMin + 1;
    const int height = yMax - yMin + 1;
    if (width <= 0 || height <= 0 || bytesPerLine <= 0) {
        result.error = "нулевой размер изображения";
        return result;
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixelCount > limits.maxPixels) {
        result.error = "изображение превышает ограничение по числу пикселей";
        result.limited = true;
        return result;
    }

    std::vector<std::uint8_t> paletteRgb;
    const bool indexed = planes == 1 && bitsPerPixel <= 8;
    if (paletteInfo == 1 && indexed) {
        if (file.size() < 769) {
            result.error = "файл слишком мал для палитры VGA";
            return result;
        }
        std::uint8_t tail[769] = {};
        if (!file.readInto(file.size() - 769, 769, tail)) {
            result.error = "не удалось прочитать палитру VGA";
            return result;
        }
        const std::uint8_t* entries = nullptr;
        if (tail[768] == 0x0C) {
            entries = tail;
        } else if (tail[0] == 0x0C) {
            entries = tail + 1;
        }
        if (entries == nullptr) {
            result.error = "маркер палитры 0x0C в конце файла отсутствует";
            return result;
        }
        paletteRgb.assign(entries, entries + 768);
    } else if (indexed) {
        const std::size_t entries = static_cast<std::size_t>(1) << bitsPerPixel;
        paletteRgb.resize(entries * 3);
        reader.reset();
        reader.skip(16);
        for (std::size_t i = 0; i < entries * 3 && reader.remaining() > 0; ++i) {
            paletteRgb[i] = reader.u8();
        }
    }

    const std::uint64_t dataStart = 128;
    const std::uint64_t available = file.size() > dataStart ? file.size() - dataStart : 0;
    const std::size_t limit =
        static_cast<std::size_t>(std::min<std::uint64_t>(available, limits.maxCompressedBytes));
    std::vector<std::uint8_t> compressed(limit);
    if (limit == 0 || !file.readInto(dataStart, limit, compressed.data())) {
        result.error = "не удалось прочитать сжатые данные";
        return result;
    }

    const std::size_t expected = static_cast<std::size_t>(bytesPerLine) *
                                 static_cast<std::size_t>(height) *
                                 static_cast<std::size_t>(planes);
    std::vector<std::uint8_t> raw;
    const RleResult rle = decodePcxRle(compressed.data(), compressed.size(), expected, raw);
    if (!rle.ok) {
        result.error = rle.error;
        return result;
    }

    result.buffer.width = width;
    result.buffer.height = height;
    result.buffer.channels = 3;
    result.buffer.pixels.assign(static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 3,
                                0);

    const std::size_t stride = static_cast<std::size_t>(bytesPerLine);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* target =
                result.buffer.pixels.data() +
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 3;
            if (planes == 1) {
                const std::uint8_t* row =
                    raw.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(bytesPerLine);
                int sample = 0;
                switch (bitsPerPixel) {
                    case 1:
                        sample = (row[x / 8] >> (7 - (x % 8))) & 1;
                        break;
                    case 2:
                        sample = (row[x / 4] >> (6 - 2 * (x % 4))) & 3;
                        break;
                    case 4:
                        sample = (row[x / 2] >> (4 - 4 * (x % 2))) & 0x0F;
                        break;
                    default:
                        sample = row[x];
                        break;
                }
                const std::size_t index = static_cast<std::size_t>(sample);
                if (index * 3 + 2 < paletteRgb.size()) {
                    target[0] = paletteRgb[index * 3];
                    target[1] = paletteRgb[index * 3 + 1];
                    target[2] = paletteRgb[index * 3 + 2];
                }
            } else {
                int component[4] = {0, 0, 0, 0};
                for (int plane = 0; plane < planes; ++plane) {
                    const std::uint64_t offset =
                        (static_cast<std::uint64_t>(y) * static_cast<std::uint64_t>(planes) +
                         static_cast<std::uint64_t>(plane)) * stride + static_cast<std::uint64_t>(x);
                    const std::uint8_t value = offset < raw.size() ? raw[static_cast<std::size_t>(offset)] : 0;
                    component[plane] = bitsPerPixel == 4 ? value * 17 : value;
                }
                target[0] = static_cast<std::uint8_t>(component[0]);
                target[1] = static_cast<std::uint8_t>(component[1]);
                target[2] = static_cast<std::uint8_t>(planes > 2 ? component[2] : component[0]);
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