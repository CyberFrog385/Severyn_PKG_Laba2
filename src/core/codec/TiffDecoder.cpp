#include "core/codec/TiffDecoder.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/ByteReader.h"
#include "core/codec/Inflate.h"
#include "core/codec/PixelAnalyzer.h"

namespace lab2 {

namespace {

class MsbBitReader {
public:
    MsbBitReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    int code(int width) {
        while (bitCount_ < width) {
            if (position_ >= size_) {
                return -1;
            }
            bitBuffer_ = (bitBuffer_ << 8) | data_[position_++];
            bitCount_ += 8;
        }
        const int value = static_cast<int>((bitBuffer_ >> (bitCount_ - width)) &
                                           ((1u << width) - 1u));
        bitCount_ -= width;
        return value;
    }

    bool failed() const { return position_ > size_; }
    std::size_t consumed() const { return position_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
};

bool decodeTiffLzw(const std::uint8_t* data, std::size_t size, std::size_t expected,
                   std::vector<std::uint8_t>& out) {
    out.clear();
    out.reserve(expected);
    std::vector<std::vector<std::uint8_t>> dictionary(4096);
    for (int i = 0; i < 256; ++i) {
        dictionary[static_cast<std::size_t>(i)] = {static_cast<std::uint8_t>(i)};
    }
    MsbBitReader reader(data, size);
    int width = 9;
    int next = 258;
    int previous = -1;
    while (true) {
        const int code = reader.code(width);
        if (code < 0 || code == 257) {
            break;
        }
        if (code == 256) {
            width = 9;
            next = 258;
            previous = -1;
            continue;
        }
        std::vector<std::uint8_t> sequence;
        if (code < next && !dictionary[static_cast<std::size_t>(code)].empty()) {
            sequence = dictionary[static_cast<std::size_t>(code)];
        } else if (code == next && previous >= 0) {
            sequence = dictionary[static_cast<std::size_t>(previous)];
            if (sequence.empty()) {
                return false;
            }
            sequence.push_back(sequence.front());
        } else {
            return false;
        }
        if (out.size() + sequence.size() > expected) {
            out.insert(out.end(), sequence.begin(), sequence.begin() +
                                                          static_cast<std::ptrdiff_t>(
                                                              expected - out.size()));
            return true;
        }
        out.insert(out.end(), sequence.begin(), sequence.end());
        if (previous >= 0 && next < 4096) {
            dictionary[static_cast<std::size_t>(next)] = dictionary[static_cast<std::size_t>(previous)];
            if (!sequence.empty()) {
                dictionary[static_cast<std::size_t>(next)].push_back(sequence.front());
            }
            ++next;
            if (next == (1 << width) - 1 && width < 12) {
                ++width;
            }
        }
        previous = code;
    }
    return out.size() >= expected || reader.failed();
}

bool decodePackBits(const std::uint8_t* data, std::size_t size, std::size_t expected,
                    std::vector<std::uint8_t>& out) {
    out.clear();
    out.reserve(expected);
    std::size_t position = 0;
    while (out.size() < expected && position < size) {
        const signed char control = static_cast<signed char>(data[position++]);
        if (control >= 0) {
            const std::size_t count = static_cast<std::size_t>(control) + 1;
            const std::size_t take = std::min(count, size - position);
            const std::size_t used = std::min(take, expected - out.size());
            out.insert(out.end(), data + position, data + position + used);
            position += take;
        } else if (control != -128) {
            const std::size_t count = static_cast<std::size_t>(-control) + 1;
            if (position >= size) {
                return false;
            }
            const std::size_t used = std::min(count, expected - out.size());
            out.insert(out.end(), used, data[position]);
            ++position;
        }
    }
    return out.size() >= expected;
}

struct TiffTags {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t samplesPerPixel = 1;
    std::uint16_t compression = 1;
    std::uint16_t photometric = 1;
    std::uint16_t planar = 1;
    std::uint16_t predictor = 1;
    std::uint16_t fillOrder = 1;
    std::uint32_t rowsPerStrip = 0;
    std::vector<std::uint32_t> stripOffsets;
    std::vector<std::uint32_t> stripByteCounts;
    bool tiled = false;
    std::vector<std::uint32_t> palette;
    bool unassociatedAlpha = false;
    bool associatedAlpha = false;
};

std::size_t typeElementSize(std::uint16_t type) {
    switch (type) {
        case 1:
        case 2:
        case 6:
        case 7:
            return 1;
        case 3:
        case 8:
            return 2;
        case 4:
        case 9:
        case 11:
            return 4;
        case 5:
        case 10:
        case 12:
            return 8;
        default:
            return 0;
    }
}

bool readIfd(FileHead& file, std::uint64_t offset, bool littleEndian, bool bigTiff, TiffTags& tags,
             std::string& error) {
    const std::size_t countSize = bigTiff ? 8 : 2;
    std::vector<std::uint8_t> countBytes(countSize);
    if (!file.readInto(offset, countSize, countBytes.data())) {
        error = "не удалось прочитать IFD";
        return false;
    }
    ByteReader countReader(countBytes.data(), countBytes.size());
    const std::uint64_t entryCount =
        bigTiff ? countReader.u64(littleEndian) : countReader.u16(littleEndian);
    if (entryCount == 0 || entryCount > 4096) {
        error = "недопустимое число тегов в IFD";
        return false;
    }

    const std::size_t entrySize = bigTiff ? 20 : 12;
    std::vector<std::uint8_t> entries(static_cast<std::size_t>(entryCount) * entrySize);
    if (!file.readInto(offset + countSize, entries.size(), entries.data())) {
        error = "не удалось прочитать записи IFD";
        return false;
    }

    for (std::uint64_t i = 0; i < entryCount; ++i) {
        ByteReader entry(entries.data() + i * entrySize, entrySize);
        const std::uint32_t tag =
            bigTiff ? static_cast<std::uint32_t>(entry.u16(littleEndian)) : entry.u16(littleEndian);
        const std::uint16_t type = entry.u16(littleEndian);
        const std::uint64_t count = bigTiff ? entry.u64(littleEndian) : entry.u32(littleEndian);
        const std::size_t elementSize = typeElementSize(type);
        const std::size_t totalBytes = elementSize * static_cast<std::size_t>(count);
        const bool inlineValue = totalBytes <= (bigTiff ? 8u : 4u);
        std::uint8_t inlineBytes[8] = {};
        std::uint64_t valueOffset = 0;
        if (inlineValue) {
            std::memcpy(inlineBytes, entry.data() + entry.pos(), bigTiff ? 8 : 4);
        } else {
            valueOffset = bigTiff ? entry.u64(littleEndian) : entry.u32(littleEndian);
        }

        const auto readAt = [&](std::size_t index) -> std::uint32_t {
            if (elementSize == 0) {
                return 0;
            }
            std::uint8_t bytes[8] = {};
            if (inlineValue) {
                std::memcpy(bytes, inlineBytes + index * elementSize,
                            std::min(sizeof(bytes), totalBytes - index * elementSize));
            } else {
                const std::size_t offset = valueOffset + index * elementSize;
                if (!file.readInto(offset, std::min<std::size_t>(elementSize, sizeof(bytes)),
                                   bytes)) {
                    return 0;
                }
            }
            ByteReader value(bytes, sizeof(bytes));
            if (type == 3 || type == 8) {
                return value.u16(littleEndian);
            }
            return value.u32(littleEndian);
        };
        const auto scalar = [&]() -> std::uint32_t { return readAt(0); };
        const auto scalarOffset = [&](std::size_t index) -> std::uint32_t { return readAt(index); };

        switch (tag) {
            case 256:
                tags.width = scalar();
                break;
            case 257:
                tags.height = scalar();
                break;
            case 258:
                tags.bitsPerSample = static_cast<std::uint16_t>(scalar());
                break;
            case 259:
                tags.compression = static_cast<std::uint16_t>(scalar());
                break;
            case 262:
                tags.photometric = static_cast<std::uint16_t>(scalar());
                break;
            case 266:
                tags.fillOrder = static_cast<std::uint16_t>(scalar());
                break;
            case 277:
                tags.samplesPerPixel = static_cast<std::uint16_t>(scalar());
                break;
            case 278:
                tags.rowsPerStrip = scalar();
                break;
            case 284:
                tags.planar = static_cast<std::uint16_t>(scalar());
                break;
            case 317:
                tags.predictor = static_cast<std::uint16_t>(scalar());
                break;
            case 322:
            case 323:
                tags.tiled = true;
                break;
            case 273:
            case 279: {
                if (count == 0 || count > 65536 || elementSize == 0) {
                    break;
                }
                const std::size_t itemBytes = bigTiff && typeElementSize(type) == 8 ? 8 : 4;
                auto& target = tag == 273 ? tags.stripOffsets : tags.stripByteCounts;
                target.reserve(static_cast<std::size_t>(count));
                for (std::uint64_t k = 0; k < count; ++k) {
                    std::uint8_t raw[8] = {};
                    if (inlineValue) {
                        std::memcpy(raw, inlineBytes, 8);
                    } else if (!file.readInto(valueOffset + k * itemBytes, itemBytes, raw)) {
                        error = tag == 273 ? "не удалось прочитать смещения полос"
                                           : "не удалось прочитать размеры полос";
                        return false;
                    }
                    ByteReader item(raw, itemBytes);
                    const std::uint64_t value = bigTiff ? item.u64(littleEndian) : item.u32(littleEndian);
                    if (value > 0xFFFFFFFFull) {
                        error = "смещение полосы не помещается в 32 бита";
                        return false;
                    }
                    target.push_back(static_cast<std::uint32_t>(value));
                }
                break;
            }
            case 320: {
                if (totalBytes == 0 || totalBytes > 65536 * 2) {
                    break;
                }
                std::vector<std::uint8_t> raw(static_cast<std::size_t>(totalBytes));
                if (inlineValue) {
                    std::memcpy(raw.data(), inlineBytes, raw.size());
                } else if (!file.readInto(valueOffset, raw.size(), raw.data())) {
                    error = "не удалось прочитать палитру ColorMap";
                    return false;
                }
                const std::size_t levels = raw.size() / 6;
                if (levels == 0) {
                    break;
                }
                tags.palette.assign(levels, 0);
                for (std::size_t k = 0; k < levels; ++k) {
                    ByteReader red(raw.data() + k * 2, 2);
                    ByteReader green(raw.data() + (levels + k) * 2, 2);
                    ByteReader blue(raw.data() + (levels * 2 + k) * 2, 2);
                    const std::uint32_t r = red.u16(littleEndian) >> 8;
                    const std::uint32_t g = green.u16(littleEndian) >> 8;
                    const std::uint32_t b = blue.u16(littleEndian) >> 8;
                    tags.palette[k] = static_cast<std::uint8_t>((r << 16) | (g << 8) | b);
                }
                break;
            }
            case 338: {
                const std::uint32_t first = scalar();
                const std::uint32_t second = count > 1 ? scalarOffset(1) : 0u;
                tags.unassociatedAlpha = first == 2;
                tags.associatedAlpha = first == 1 || second == 1;
                break;
            }
            default:
                break;
        }
    }

    if (tags.tiled) {
        error = "тайловые TIFF не поддерживаются";
        return false;
    }
    if (tags.stripOffsets.empty()) {
        error = "в IFD нет смещений полос";
        return false;
    }
    if (tags.planar != 1) {
        error = "раздельное хранение плоскостей не поддерживается";
        return false;
    }
    return true;
}

int sampleFromBytes(const std::uint8_t* row, std::size_t index, int bits) {
    if (bits == 8) {
        return row[index];
    }
    if (bits == 16) {
        return row[index * 2];
    }
    const int perByte = 8 / bits;
    const int shift = 8 - bits * (static_cast<int>(index % static_cast<std::size_t>(perByte)) + 1);
    return (row[index / static_cast<std::size_t>(perByte)] >> shift) & ((1 << bits) - 1);
}

int scaleChannel(int value, int bits) {
    return bits >= 8 ? value : value * 255 / ((1 << bits) - 1);
}

}

DecodeResult decodeTiff(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    DecodeResult result;
    result.decoder = "TIFF (полосы, PackBits, LZW, DEFLATE)";

    std::vector<std::uint8_t> head;
    if (!file.readAt(0, 8, head)) {
        result.error = "файл слишком мал для TIFF";
        return result;
    }
    ByteReader reader(head.data(), head.size());
    const std::string order = reader.rawString(2);
    bool littleEndian = true;
    if (order == "II") {
        littleEndian = true;
    } else if (order == "MM") {
        littleEndian = false;
    } else {
        result.error = "неизвестный порядок байтов TIFF";
        return result;
    }
    const std::uint16_t magic = reader.u16(littleEndian);
    if (magic != 42 && magic != 43) {
        result.error = "неизвестная версия TIFF";
        return result;
    }
    const bool bigTiff = magic == 43;
    const std::uint64_t ifdOffset = bigTiff ? reader.u64(littleEndian) : reader.u32(littleEndian);

    TiffTags tags;
    std::string error;
    if (!readIfd(file, ifdOffset, littleEndian, bigTiff, tags, error)) {
        result.error = error;
        return result;
    }
    if (tags.width == 0 || tags.height == 0) {
        result.error = "нулевой размер изображения";
        return result;
    }

    const int bits = tags.bitsPerSample == 0 ? 1 : tags.bitsPerSample;
    if (bits != 1 && bits != 8 && bits != 16) {
        result.error = "поддерживаются только 1, 8 и 16 бит на канал";
        result.unsupported = true;
        return result;
    }
    const int samples = std::clamp(static_cast<int>(tags.samplesPerPixel), 1, 4);
    if (tags.photometric == 6) {
        result.error = "цветовая модель YCbCr в TIFF не поддерживается";
        result.unsupported = true;
        return result;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(tags.width) * static_cast<std::uint64_t>(tags.height);
    if (pixelCount > limits.maxPixels) {
        result.error = "изображение превышает ограничение по числу пикселей";
        result.limited = true;
        return result;
    }

    const std::uint32_t rowsPerStrip =
        tags.rowsPerStrip > 0 && tags.rowsPerStrip < tags.height ? tags.rowsPerStrip : tags.height;
    const std::size_t rowBytes =
        (static_cast<std::size_t>(tags.width) * static_cast<std::size_t>(samples) *
             static_cast<std::size_t>(bits) + 7) / 8;

    const bool alpha =
        samples == 4 && tags.photometric != 3 && (tags.unassociatedAlpha || tags.associatedAlpha);
    result.buffer.width = static_cast<int>(tags.width);
    result.buffer.height = static_cast<int>(tags.height);
    result.buffer.channels = alpha ? 4 : 3;
    result.buffer.pixels.assign(static_cast<std::size_t>(tags.width) *
                                    static_cast<std::size_t>(tags.height) *
                                    static_cast<std::size_t>(result.buffer.channels),
                                0);

    std::vector<std::uint8_t> strip;
    std::vector<std::uint8_t> plain;
    std::uint32_t row = 0;
    for (std::size_t s = 0; s < tags.stripOffsets.size() && row < tags.height; ++s) {
        const std::uint32_t count = s < tags.stripByteCounts.size()
                                        ? tags.stripByteCounts[s]
                                        : static_cast<std::uint32_t>(rowBytes * rowsPerStrip);
        if (count == 0 || count > limits.maxCompressedBytes) {
            result.error = "размер полосы превышает допустимый предел";
            return result;
        }
        strip.assign(count, 0);
        if (!file.readInto(tags.stripOffsets[s], count, strip.data())) {
            result.error = "не удалось прочитать полосу";
            return result;
        }
        const std::uint32_t rows = std::min(rowsPerStrip, tags.height - row);
        const std::size_t expected = rowBytes * static_cast<std::size_t>(rows);
        switch (tags.compression) {
            case 1:
                plain = strip;
                break;
            case 5:
                if (!decodeTiffLzw(strip.data(), count, expected, plain)) {
                    result.error = "поток LZW в TIFF повреждён";
                    return result;
                }
                break;
            case 8:
            case 32946: {
                const InflateResult inflated = zlibInflate(strip.data(), count, plain, expected + 64);
                if (!inflated.ok) {
                    result.error = inflated.error;
                    return result;
                }
                break;
            }
            case 32773:
                if (!decodePackBits(strip.data(), count, expected, plain)) {
                    result.error = "поток PackBits в TIFF повреждён";
                    return result;
                }
                break;
            default:
                result.error = "неподдерживаемый алгоритм сжатия TIFF";
                result.unsupported = true;
                return result;
        }

        for (std::uint32_t y = 0; y < rows; ++y) {
            const std::size_t rowOffset = static_cast<std::size_t>(y) * rowBytes;
            if (rowOffset + rowBytes > plain.size()) {
                result.error = "полоса короче заявленного числа строк";
                return result;
            }
            std::vector<std::uint8_t> current(plain.begin() + static_cast<std::ptrdiff_t>(rowOffset),
                                               plain.begin() +
                                                   static_cast<std::ptrdiff_t>(rowOffset + rowBytes));
            if (tags.fillOrder == 2) {
                for (std::uint8_t& value : current) {
                    std::uint8_t reversed = 0;
                    for (int bit = 0; bit < 8; ++bit) {
                        if (((value >> bit) & 1) != 0) {
                            reversed = static_cast<std::uint8_t>(reversed | (1u << (7 - bit)));
                        }
                    }
                    value = reversed;
                }
            }
            if (tags.predictor == 2 && bits == 8) {
                for (std::size_t k = static_cast<std::size_t>(samples); k < current.size(); ++k) {
                    current[k] = static_cast<std::uint8_t>(current[k] + current[k - samples]);
                }
            } else if (tags.predictor == 2 && bits == 16) {
                const std::size_t stride = static_cast<std::size_t>(samples) * 2;
                for (std::size_t k = stride; k + 2 <= current.size(); k += 2) {
                    const std::uint16_t sum =
                        static_cast<std::uint16_t>((current[k] << 8) | current[k + 1]);
                    const std::uint16_t previousValue = static_cast<std::uint16_t>(
                        (current[k - stride] << 8) | current[k - stride + 1]);
                    const std::uint16_t value = static_cast<std::uint16_t>(sum + previousValue);
                    current[k] = static_cast<std::uint8_t>(value >> 8);
                    current[k + 1] = static_cast<std::uint8_t>(value & 0xFF);
                }
            }

            for (std::uint32_t x = 0; x < tags.width; ++x) {
                std::uint8_t* target =
                    result.buffer.pixels.data() +
                    (static_cast<std::size_t>(row + y) * static_cast<std::size_t>(tags.width) +
                     static_cast<std::size_t>(x)) *
                        static_cast<std::size_t>(result.buffer.channels);
                const std::size_t base = static_cast<std::size_t>(x) * static_cast<std::size_t>(samples);
                if (tags.photometric == 3) {
                    const std::size_t index =
                        static_cast<std::size_t>(sampleFromBytes(current.data(), base, bits));
                    if (index < tags.palette.size()) {
                        const std::uint32_t color = tags.palette[index];
                        target[0] = static_cast<std::uint8_t>(color >> 16);
                        target[1] = static_cast<std::uint8_t>(color >> 8);
                        target[2] = static_cast<std::uint8_t>(color);
                    }
                    continue;
                }
                int component[4] = {0, 0, 0, 0};
                for (int c = 0; c < samples; ++c) {
                    component[c] = sampleFromBytes(current.data(), base + static_cast<std::size_t>(c), bits);
                }
                if (tags.photometric == 0) {
                    const int gray = 255 - scaleChannel(component[0], bits);
                    target[0] = static_cast<std::uint8_t>(gray);
                    target[1] = static_cast<std::uint8_t>(gray);
                    target[2] = static_cast<std::uint8_t>(gray);
                } else if (samples < 3) {
                    const int gray = std::clamp(scaleChannel(component[0], bits), 0, 255);
                    target[0] = static_cast<std::uint8_t>(gray);
                    target[1] = static_cast<std::uint8_t>(gray);
                    target[2] = static_cast<std::uint8_t>(gray);
                } else if (tags.photometric == 5) {
                    const int k = samples > 3 ? component[3] : 0;
                    target[0] = static_cast<std::uint8_t>(std::clamp(255 - component[0] - k, 0, 255));
                    target[1] = static_cast<std::uint8_t>(std::clamp(255 - component[1] - k, 0, 255));
                    target[2] = static_cast<std::uint8_t>(std::clamp(255 - component[2] - k, 0, 255));
                } else {
                    target[0] = static_cast<std::uint8_t>(scaleChannel(component[0], bits));
                    target[1] = static_cast<std::uint8_t>(scaleChannel(component[1], bits));
                    target[2] = static_cast<std::uint8_t>(scaleChannel(component[2], bits));
                }
                if (alpha) {
                    target[3] = tags.unassociatedAlpha
                                    ? static_cast<std::uint8_t>(std::clamp(component[3], 0, 255))
                                    : static_cast<std::uint8_t>(std::clamp(component[0], 0, 255));
                }
            }
        }
        row += rows;
    }

    if (row < tags.height) {
        result.error = "изображение не полностью покрыто полосами";
        return result;
    }

    result.stats = analyzeRgb(result.buffer, record);
    result.ok = result.buffer.valid();
    if (!result.ok) {
        result.error = "не удалось собрать изображение целиком";
    }
    return result;
}

}