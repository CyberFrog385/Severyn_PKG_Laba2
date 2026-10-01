#include "core/codec/JpegDecoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "core/ByteReader.h"
#include "core/codec/PixelAnalyzer.h"

namespace lab2 {

namespace {

constexpr int kZigZag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

const double* idctTable() {
    static std::array<double, 64> table{};
    static bool ready = false;
    if (!ready) {
        for (int u = 0; u < 8; ++u) {
            for (int x = 0; x < 8; ++x) {
                const double scale = u == 0 ? std::sqrt(0.125) : 0.5;
                table[static_cast<std::size_t>(u * 8 + x)] =
                    scale * std::cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0);
            }
        }
        ready = true;
    }
    return table.data();
}

void inverseDct(double* block) {
    const double* table = idctTable();
    double rows[64] = {};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            double sum = 0.0;
            for (int u = 0; u < 8; ++u) {
                sum += table[u * 8 + x] * block[y * 8 + u];
            }
            rows[y * 8 + x] = sum;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            double sum = 0.0;
            for (int v = 0; v < 8; ++v) {
                sum += table[v * 8 + y] * rows[v * 8 + x];
            }
            block[y * 8 + x] = sum + 128.0;
        }
    }
}

std::vector<std::uint8_t> upsampleFactor2(const std::uint8_t* src, int width, int height,
                                          int factorX, int factorY) {
    std::vector<std::uint8_t> current(src, src + static_cast<std::size_t>(width) *
                                                    static_cast<std::size_t>(height));
    int currentWidth = width;
    if (factorX == 2) {
        std::vector<std::uint8_t> wide(static_cast<std::size_t>(width) * 2 *
                                       static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* row =
                current.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            std::uint8_t* out =
                wide.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width * 2);
            for (int x = 0; x < width; ++x) {
                const int centre = row[x];
                const int before = row[x > 0 ? x - 1 : 0];
                const int after = row[x + 1 < width ? x + 1 : width - 1];
                out[x * 2] = static_cast<std::uint8_t>((3 * centre + before + 2) >> 2);
                out[x * 2 + 1] = static_cast<std::uint8_t>((3 * centre + after + 1) >> 2);
            }
        }
        current = std::move(wide);
        currentWidth = width * 2;
    }
    if (factorY == 2) {
        std::vector<std::uint8_t> tall(static_cast<std::size_t>(currentWidth) * 2 *
                                       static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* centre =
                current.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(currentWidth);
            const std::uint8_t* above =
                current.data() +
                static_cast<std::size_t>(y > 0 ? y - 1 : 0) * static_cast<std::size_t>(currentWidth);
            const std::uint8_t* below =
                current.data() + static_cast<std::size_t>(y + 1 < height ? y + 1 : height - 1) *
                                     static_cast<std::size_t>(currentWidth);
            std::uint8_t* even =
                tall.data() + static_cast<std::size_t>(y * 2) * static_cast<std::size_t>(currentWidth);
            std::uint8_t* odd = tall.data() +
                               static_cast<std::size_t>(y * 2 + 1) * static_cast<std::size_t>(currentWidth);
            for (int x = 0; x < currentWidth; ++x) {
                even[x] = static_cast<std::uint8_t>((3 * centre[x] + above[x] + 2) >> 2);
                odd[x] = static_cast<std::uint8_t>((3 * centre[x] + below[x] + 1) >> 2);
            }
        }
        current = std::move(tall);
    }
    return current;
}

class EntropyReader {
public:
    EntropyReader(const std::uint8_t* data, std::size_t size, std::size_t position)
        : data_(data), size_(size), position_(position) {}

    int nextByte() {
        if (position_ >= size_) {
            marker_ = true;
            return -1;
        }
        std::uint8_t value = data_[position_];
        if (value == 0xFF) {
            if (position_ + 1 >= size_) {
                marker_ = true;
                return -1;
            }
            const std::uint8_t next = data_[position_ + 1];
            if (next == 0x00) {
                position_ += 2;
                return 0xFF;
            }
            if (next == 0xFF) {
                ++position_;
                return nextByte();
            }
            marker_ = true;
            return -1;
        }
        ++position_;
        return value;
    }

    int bit() {
        if (bitCount_ == 0) {
            const int value = nextByte();
            if (value < 0) {
                return -1;
            }
            bitBuffer_ = static_cast<std::uint8_t>(value);
            bitCount_ = 8;
        }
        --bitCount_;
        return (bitBuffer_ >> bitCount_) & 1;
    }

    int receive(int length) {
        int value = 0;
        for (int i = 0; i < length; ++i) {
            const int b = bit();
            if (b < 0) {
                return value << (length - i);
            }
            value = (value << 1) | b;
        }
        return value;
    }

    void align() { bitCount_ = 0; }

    bool atMarker() const { return marker_; }

    void clearMarker() {
        marker_ = false;
        if (position_ + 1 < size_ && data_[position_] == 0xFF && data_[position_ + 1] >= 0xD0 &&
            data_[position_ + 1] <= 0xD7) {
            position_ += 2;
        }
    }

    std::size_t position() const { return position_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::uint8_t bitBuffer_ = 0;
    int bitCount_ = 0;
    bool marker_ = false;
};

struct HuffmanTable {
    std::array<int, 17> counts{};
    std::vector<std::uint8_t> values;
    std::array<int, 17> minimum{};
    std::array<int, 17> maximum{};
    std::array<int, 17> valuePointer{};
    bool present = false;

    void build() {
        int code = 0;
        int index = 0;
        for (int length = 1; length <= 16; ++length) {
            valuePointer[static_cast<std::size_t>(length)] = index;
            minimum[static_cast<std::size_t>(length)] = code;
            code += counts[static_cast<std::size_t>(length)];
            index += counts[static_cast<std::size_t>(length)];
            maximum[static_cast<std::size_t>(length)] = counts[static_cast<std::size_t>(length)] == 0
                                                           ? -1
                                                           : code - 1;
            code <<= 1;
        }
        present = !values.empty();
    }

    int decode(EntropyReader& reader) const {
        int code = 0;
        int length = 1;
        while (length <= 16) {
            const int bit = reader.bit();
            if (bit < 0) {
                return -1;
            }
            code = (code << 1) | bit;
            const int high = maximum[static_cast<std::size_t>(length)];
            if (high >= 0 && code <= high) {
                const int slot = valuePointer[static_cast<std::size_t>(length)] + code -
                                 minimum[static_cast<std::size_t>(length)];
                if (slot < 0 || slot >= static_cast<int>(values.size())) {
                    return -1;
                }
                return values[static_cast<std::size_t>(slot)];
            }
            ++length;
        }
        return -1;
    }
};

struct QuantTable {
    std::array<int, 64> values{};
    bool present = false;
};

struct Component {
    int id = 0;
    int horizontal = 1;
    int vertical = 1;
    int quantTable = 0;
    int dcTable = 0;
    int acTable = 0;
    int dcPredictor = 0;
    int planeWidth = 0;
    int planeHeight = 0;
    std::vector<std::uint8_t> plane;
};

int extendValue(int value, int length) {
    return value < (1 << (length - 1)) ? value - (1 << length) + 1 : value;
}

}

DecodeResult decodeJpeg(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    DecodeResult result;
    result.decoder = "JPEG (Хаффман, IDCT, апсэмплинг цветности)";

    std::vector<std::uint8_t> data;
    if (!file.readAll(data, limits.maxCompressedBytes)) {
        result.error = "не удалось прочитать файл: " + file.lastError();
        return result;
    }
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        result.error = "отсутствует маркер SOI";
        return result;
    }

    QuantTable quantTables[4];
    HuffmanTable dcTables[4];
    HuffmanTable acTables[4];
    std::vector<Component> components;
    int frameWidth = 0;
    int frameHeight = 0;
    int restartInterval = 0;
    bool progressive = false;
    std::size_t scanStart = 0;

    std::size_t position = 2;
    while (position + 1 < data.size()) {
        if (data[position] != 0xFF) {
            ++position;
            continue;
        }
        const std::uint8_t marker = data[position + 1];
        position += 2;
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue;
        }
        if (marker == 0xDA) {
            if (position + 4 <= data.size()) {
                const std::size_t headerLength =
                    (static_cast<std::size_t>(data[position]) << 8) | data[position + 1];
                if (headerLength >= 6 && position + headerLength <= data.size()) {
                    ByteReader header(data.data() + position + 2, headerLength - 2);
                    const int scanComponents = header.u8();
                    for (int i = 0; i < scanComponents && header.ok(); ++i) {
                        const int id = header.u8();
                        const int tables = header.u8();
                        for (Component& component : components) {
                            if (component.id == id) {
                                component.dcTable = tables >> 4;
                                component.acTable = tables & 0x0F;
                            }
                        }
                    }
                }
                scanStart = position + headerLength;
            }
            break;
        }
        if (position + 2 > data.size()) {
            break;
        }
        ByteReader lengthReader(data.data() + position, 2);
        const int segmentLength = lengthReader.u16be();
        if (segmentLength < 2 || position + static_cast<std::size_t>(segmentLength) > data.size()) {
            result.error = "повреждена длина сегмента JPEG";
            return result;
        }
        ByteReader lengthField(data.data() + position, 2);
        lengthField.u16be();
        ByteReader segment(data.data() + position + 2,
                           static_cast<std::size_t>(segmentLength) - 2);

        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            progressive = marker == 0xC2;
            const int precision = segment.u8();
            frameHeight = segment.u16be();
            frameWidth = segment.u16be();
            const int count = segment.u8();
            if (precision != 8) {
                result.error = "поддерживается только 8-битная точность";
                return result;
            }
            if (count < 1 || count > 4) {
                result.error = "неподдерживаемое число компонентов";
                result.unsupported = true;
                return result;
            }
            for (int i = 0; i < count; ++i) {
                Component component;
                component.id = segment.u8();
                const int sampling = segment.u8();
                component.horizontal = sampling >> 4;
                component.vertical = sampling & 0x0F;
                component.quantTable = segment.u8();
                if (component.horizontal < 1 || component.horizontal > 4 ||
                    component.vertical < 1 || component.vertical > 4) {
                    result.error = "недопустимые коэффициенты дискретизации";
                    return result;
                }
                components.push_back(component);
            }
        } else if (marker == 0xC4) {
            while (segment.remaining() > 0) {
                const int spec = segment.u8();
                const int index = spec & 0x0F;
                const int kind = spec >> 4;
                if (index < 0 || index > 3 || (kind != 0 && kind != 1)) {
                    result.error = "неверный идентификатор таблицы Хаффмана";
                    return result;
                }
                HuffmanTable table;
                int total = 0;
                for (int length = 1; length <= 16; ++length) {
                    const int count = segment.u8();
                    table.counts[static_cast<std::size_t>(length)] = count;
                    total += count;
                }
                table.values.resize(static_cast<std::size_t>(total));
                for (int i = 0; i < total; ++i) {
                    table.values[static_cast<std::size_t>(i)] = segment.u8();
                }
                table.build();
                if (kind == 0) {
                    dcTables[index] = std::move(table);
                } else {
                    acTables[index] = std::move(table);
                }
            }
        } else if (marker == 0xDB) {
            while (segment.remaining() > 0) {
                const int spec = segment.u8();
                const int precision = spec >> 4;
                const int index = spec & 0x0F;
                if (index < 0 || index > 3) {
                    result.error = "неверный индекс таблицы квантования";
                    return result;
                }
                QuantTable table;
                for (int i = 0; i < 64; ++i) {
                    table.values[static_cast<std::size_t>(i)] =
                        precision == 0 ? segment.u8() : segment.u16be();
                }
                table.present = true;
                quantTables[index] = table;
            }
        } else if (marker == 0xDD) {
            restartInterval = segment.u16be();
        } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
                   marker != 0xCC) {
            progressive = true;
        }

        position += static_cast<std::size_t>(segmentLength);
    }

    if (frameWidth <= 0 || frameHeight <= 0) {
        result.error = "не найден кадр SOF";
        return result;
    }
    if (progressive) {
        result.error = "прогрессивный JPEG: разбор заголовков выполнен, пиксели не восстановлены";
        result.unsupported = true;
        return result;
    }
    if (components.size() != 3 && components.size() != 1) {
        result.error = "поддерживаются только 1 или 3 компоненты";
        result.unsupported = true;
        return result;
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(frameWidth) * static_cast<std::uint64_t>(frameHeight);
    if (pixelCount > limits.maxPixels) {
        result.error = "изображение превышает ограничение по числу пикселей";
        result.limited = true;
        return result;
    }

    for (const Component& component : components) {
        if (!quantTables[component.quantTable].present) {
            result.error = "отсутствует таблица квантования";
            return result;
        }
        if (!dcTables[component.dcTable].present || !acTables[component.acTable].present) {
            result.error = "отсутствует таблица Хаффмана";
            return result;
        }
    }

    int maxHorizontal = 1;
    int maxVertical = 1;
    for (const Component& component : components) {
        maxHorizontal = std::max(maxHorizontal, component.horizontal);
        maxVertical = std::max(maxVertical, component.vertical);
    }
    const int mcuWidth = maxHorizontal * 8;
    const int mcuHeight = maxVertical * 8;
    const int mcuCountX = (frameWidth + mcuWidth - 1) / mcuWidth;
    const int mcuCountY = (frameHeight + mcuHeight - 1) / mcuHeight;

    for (Component& component : components) {
        component.planeWidth = mcuCountX * component.horizontal * 8;
        component.planeHeight = mcuCountY * component.vertical * 8;
        component.plane.assign(static_cast<std::size_t>(component.planeWidth) *
                                   static_cast<std::size_t>(component.planeHeight),
                               128);
    }

    EntropyReader reader(data.data(), data.size(), scanStart);
    bool truncated = false;
    int mcuIndex = 0;
    const int totalMcu = mcuCountX * mcuCountY;
    double block[64];
    while (mcuIndex < totalMcu) {
        if (restartInterval > 0 && mcuIndex > 0 && mcuIndex % restartInterval == 0) {
            reader.align();
            reader.clearMarker();
            for (Component& component : components) {
                component.dcPredictor = 0;
            }
        }
        const int mcuX = mcuIndex % mcuCountX;
        const int mcuY = mcuIndex / mcuCountX;
        for (Component& component : components) {
            const QuantTable& quant = quantTables[component.quantTable];
            for (int blockY = 0; blockY < component.vertical; ++blockY) {
                for (int blockX = 0; blockX < component.horizontal; ++blockX) {
                    const int dcLength = dcTables[component.dcTable].decode(reader);
                    if (dcLength < 0) {
                        truncated = true;
                        break;
                    }
                    int difference = 0;
                    if (dcLength > 0) {
                        const int raw = reader.receive(dcLength);
                        if (reader.atMarker()) {
                            truncated = true;
                            break;
                        }
                        difference = extendValue(raw, dcLength);
                    }
                    component.dcPredictor += difference;
                    for (int i = 0; i < 64; ++i) {
                        block[i] = 0.0;
                    }
                    block[0] = static_cast<double>(component.dcPredictor) *
                               quant.values[0];
                    int index = 1;
                    while (index < 64) {
                        const int symbol = acTables[component.acTable].decode(reader);
                        if (symbol < 0) {
                            truncated = true;
                            break;
                        }
                        const int size = symbol & 0x0F;
                        const int run = symbol >> 4;
                        if (size == 0) {
                            if (run == 15) {
                                index += 16;
                                continue;
                            }
                            break;
                        }
                        index += run;
                        if (index > 63) {
                            break;
                        }
                        const int raw = reader.receive(size);
                        if (reader.atMarker()) {
                            truncated = true;
                            break;
                        }
                        block[kZigZag[index]] =
                            static_cast<double>(extendValue(raw, size)) *
                            quant.values[static_cast<std::size_t>(index)];
                        ++index;
                    }
                    if (truncated) {
                        break;
                    }
                    inverseDct(block);
                    const int originX = (mcuX * component.horizontal + blockX) * 8;
                    const int originY = (mcuY * component.vertical + blockY) * 8;
                    for (int y = 0; y < 8; ++y) {
                        for (int x = 0; x < 8; ++x) {
                            const int value = static_cast<int>(std::lround(block[y * 8 + x]));
                            component.plane[static_cast<std::size_t>(originY + y) *
                                                static_cast<std::size_t>(component.planeWidth) +
                                            static_cast<std::size_t>(originX + x)] =
                                static_cast<std::uint8_t>(std::clamp(value, 0, 255));
                        }
                    }
                }
                if (truncated) {
                    break;
                }
            }
            if (truncated) {
                break;
            }
        }
        if (truncated) {
            break;
        }
        ++mcuIndex;
    }

    result.buffer.width = frameWidth;
    result.buffer.height = frameHeight;
    result.buffer.channels = 3;
    result.buffer.pixels.assign(static_cast<std::size_t>(frameWidth) *
                                    static_cast<std::size_t>(frameHeight) * 3,
                                0);
    std::vector<std::vector<std::uint8_t>> expanded(components.size());
    std::vector<int> expandedWidth(components.size(), 0);
    for (std::size_t c = 0; c < components.size(); ++c) {
        Component& component = components[c];
        const int factorX = maxHorizontal / component.horizontal;
        const int factorY = maxVertical / component.vertical;
        expanded[c] = upsampleFactor2(component.plane.data(), component.planeWidth,
                                      component.planeHeight, factorX, factorY);
        expandedWidth[c] = component.planeWidth * factorX;
    }
    for (int y = 0; y < frameHeight; ++y) {
        for (int x = 0; x < frameWidth; ++x) {
            std::uint8_t* target =
                result.buffer.pixels.data() +
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(frameWidth) +
                 static_cast<std::size_t>(x)) * 3;
            if (components.size() == 1) {
                const Component& component = components.front();
                const int sampleX = x * component.horizontal / maxHorizontal;
                const int sampleY = y * component.vertical / maxVertical;
                const std::uint8_t value =
                    component.plane[static_cast<std::size_t>(sampleY) *
                                        static_cast<std::size_t>(component.planeWidth) +
                                    static_cast<std::size_t>(sampleX)];
                target[0] = value;
                target[1] = value;
                target[2] = value;
                continue;
            }
            int sample[3] = {0, 0, 0};
            for (int c = 0; c < 3; ++c) {
                const std::size_t index = static_cast<std::size_t>(c);
                const int rows = static_cast<int>(expanded[index].size()) /
                                 std::max(1, expandedWidth[index]);
                const int sampleX = std::min(expandedWidth[index] - 1, x);
                const int sampleY = std::min(rows - 1, y);
                sample[c] = expanded[index][static_cast<std::size_t>(sampleY) *
                                                 static_cast<std::size_t>(expandedWidth[index]) +
                                             static_cast<std::size_t>(sampleX)];
            }
            const double luminance = static_cast<double>(sample[0]);
            const double cb = static_cast<double>(sample[1]) - 128.0;
            const double cr = static_cast<double>(sample[2]) - 128.0;
            const int red = static_cast<int>(std::lround(luminance + 1.402 * cr));
            const int green = static_cast<int>(std::lround(luminance - 0.344136 * cb - 0.714136 * cr));
            const int blue = static_cast<int>(std::lround(luminance + 1.772 * cb));
            target[0] = static_cast<std::uint8_t>(std::clamp(red, 0, 255));
            target[1] = static_cast<std::uint8_t>(std::clamp(green, 0, 255));
            target[2] = static_cast<std::uint8_t>(std::clamp(blue, 0, 255));
        }
    }

    result.ok = result.buffer.valid();
    if (!result.ok) {
        result.error = "не удалось собрать изображение целиком";
        return result;
    }
    result.stats = analyzeRgb(result.buffer, record);
    if (truncated) {
        result.stats.error = "часть энтропийных данных отсутствует, изображение восстановлено частично";
    }
    return result;
}

}