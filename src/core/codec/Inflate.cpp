#include "core/codec/Inflate.h"

#include <array>
#include <cstring>
#include <string>

namespace lab2 {

namespace {

constexpr int kMaxBits = 15;

class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    int bit() {
        if (bitCount_ == 0) {
            if (position_ >= size_) {
                failed_ = true;
                return 0;
            }
            bitBuffer_ = data_[position_++];
            bitCount_ = 8;
        }
        const int value = bitBuffer_ & 1;
        bitBuffer_ >>= 1;
        --bitCount_;
        return value;
    }

    std::uint32_t bits(int count) {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            value |= static_cast<std::uint32_t>(bit()) << i;
        }
        return value;
    }

    void alignToByte() { bitCount_ = 0; }

    bool copy(std::size_t count, std::vector<std::uint8_t>& out) {
        if (position_ + count > size_) {
            failed_ = true;
            return false;
        }
        out.insert(out.end(), data_ + position_, data_ + position_ + count);
        position_ += count;
        return true;
    }

    bool failed() const { return failed_; }
    std::size_t position() const { return position_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
    bool failed_ = false;
};

struct Huffman {
    std::array<std::uint16_t, kMaxBits + 1> counts{};
    std::vector<std::uint16_t> symbols;

    bool build(const std::uint8_t* lengths, std::size_t count) {
        counts.fill(0);
        for (std::size_t i = 0; i < count; ++i) {
            if (lengths[i] > kMaxBits) {
                return false;
            }
            ++counts[lengths[i]];
        }
        if (counts[0] == count) {
            symbols.clear();
            return true;
        }
        std::array<std::uint16_t, kMaxBits + 1> offsets{};
        std::uint16_t offset = 0;
        for (int bits = 1; bits <= kMaxBits; ++bits) {
            offsets[static_cast<std::size_t>(bits)] = offset;
            offset = static_cast<std::uint16_t>(offset + counts[static_cast<std::size_t>(bits)]);
        }
        symbols.assign(offset, 0);
        for (std::size_t i = 0; i < count; ++i) {
            if (lengths[i] != 0) {
                symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
            }
        }
        return true;
    }
};

int decodeSymbol(BitReader& reader, const Huffman& tree) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int length = 1; length <= kMaxBits; ++length) {
        code |= reader.bit();
        const int count = tree.counts[static_cast<std::size_t>(length)];
        if (code - count < first) {
            return tree.symbols[static_cast<std::size_t>(index + (code - first))];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

const std::array<std::uint16_t, 29>& lengthBase() {
    static const std::array<std::uint16_t, 29> table{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                                     15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                                     67, 83, 99, 115, 131, 163, 195, 227, 258};
    return table;
}

const std::array<std::uint8_t, 29>& lengthExtra() {
    static const std::array<std::uint8_t, 29> table{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    return table;
}

const std::array<std::uint16_t, 30>& distanceBase() {
    static const std::array<std::uint16_t, 30> table{1,    2,    3,    4,    5,    7,     9,
                                                     13,   17,   25,   33,   49,   65,    97,
                                                     129,  193,  257,  385,  513,  769,   1025,
                                                     1537, 2049, 3073, 4097, 6145, 8193,  12289,
                                                     16385, 24577};
    return table;
}

const std::array<std::uint8_t, 30>& distanceExtra() {
    static const std::array<std::uint8_t, 30> table{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,
                                                    4, 4, 5, 5, 6, 6, 7, 7,  8,  8,
                                                    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    return table;
}

void buildFixedTrees(Huffman& literals, Huffman& distances) {
    std::array<std::uint8_t, 288> literalLengths{};
    for (int i = 0; i < 144; ++i) {
        literalLengths[static_cast<std::size_t>(i)] = 8;
    }
    for (int i = 144; i < 256; ++i) {
        literalLengths[static_cast<std::size_t>(i)] = 9;
    }
    for (int i = 256; i < 280; ++i) {
        literalLengths[static_cast<std::size_t>(i)] = 7;
    }
    for (int i = 280; i < 288; ++i) {
        literalLengths[static_cast<std::size_t>(i)] = 8;
    }
    literals.build(literalLengths.data(), literalLengths.size());
    std::array<std::uint8_t, 30> distanceLengths{};
    distanceLengths.fill(5);
    distances.build(distanceLengths.data(), distanceLengths.size());
}

bool readDynamicTrees(BitReader& reader, Huffman& literals, Huffman& distances) {
    static const std::array<std::uint8_t, 19> order{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
    const int literalCount = static_cast<int>(reader.bits(5)) + 257;
    const int distanceCount = static_cast<int>(reader.bits(5)) + 1;
    const int codeCount = static_cast<int>(reader.bits(4)) + 4;
    if (literalCount > 286 || distanceCount > 30) {
        return false;
    }
    std::array<std::uint8_t, 19> codeLengths{};
    for (int i = 0; i < codeCount; ++i) {
        codeLengths[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])] =
            static_cast<std::uint8_t>(reader.bits(3));
    }
    Huffman codeTree;
    if (!codeTree.build(codeLengths.data(), codeLengths.size())) {
        return false;
    }
    std::vector<std::uint8_t> lengths(static_cast<std::size_t>(literalCount + distanceCount), 0);
    int index = 0;
    while (index < literalCount + distanceCount) {
        const int symbol = decodeSymbol(reader, codeTree);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(symbol);
            continue;
        }
        int repeat = 0;
        std::uint8_t value = 0;
        if (symbol == 16) {
            if (index == 0) {
                return false;
            }
            value = lengths[static_cast<std::size_t>(index - 1)];
            repeat = 3 + static_cast<int>(reader.bits(2));
        } else if (symbol == 17) {
            repeat = 3 + static_cast<int>(reader.bits(3));
        } else {
            repeat = 11 + static_cast<int>(reader.bits(7));
        }
        if (index + repeat > literalCount + distanceCount) {
            return false;
        }
        for (int i = 0; i < repeat; ++i) {
            lengths[static_cast<std::size_t>(index++)] = value;
        }
    }
    return literals.build(lengths.data(), static_cast<std::size_t>(literalCount)) &&
           distances.build(lengths.data() + literalCount, static_cast<std::size_t>(distanceCount));
}

}

InflateResult inflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                      std::size_t maxOutput) {
    InflateResult result;
    out.clear();
    out.reserve(size * 4);
    BitReader reader(data, size);
    while (true) {
        const int final = reader.bit();
        const int type = static_cast<int>(reader.bits(2));
        if (reader.failed()) {
            result.error = "данные обрываются на границе блока";
            return result;
        }
        if (type == 0) {
            reader.alignToByte();
            const std::uint32_t length = reader.bits(16);
            reader.bits(16);
            if (out.size() + length > maxOutput) {
                result.error = "распакованный размер превышает ожидаемый";
                return result;
            }
            if (!reader.copy(length, out)) {
                result.error = "несохранённый блок обрезан";
                return result;
            }
        } else if (type == 1 || type == 2) {
            Huffman literals;
            Huffman distances;
            if (type == 1) {
                buildFixedTrees(literals, distances);
            } else if (!readDynamicTrees(reader, literals, distances)) {
                result.error = "повреждено описание динамических деревьев Хаффмана";
                return result;
            }
            while (true) {
                const int symbol = decodeSymbol(reader, literals);
                if (symbol < 0) {
                    result.error = "неверный код Хаффмана";
                    return result;
                }
                if (symbol < 256) {
                    if (out.size() + 1 > maxOutput) {
                        result.error = "распакованный размер превышает ожидаемый";
                        return result;
                    }
                    out.push_back(static_cast<std::uint8_t>(symbol));
                    continue;
                }
                if (symbol == 256) {
                    break;
                }
                const int lengthIndex = symbol - 257;
                if (lengthIndex >= 29) {
                    result.error = "недопустимый код длины";
                    return result;
                }
                const std::size_t length = static_cast<std::size_t>(lengthBase()[static_cast<std::size_t>(lengthIndex)]) +
                                           reader.bits(lengthExtra()[static_cast<std::size_t>(lengthIndex)]);
                const int distanceSymbol = decodeSymbol(reader, distances);
                if (distanceSymbol < 0 || distanceSymbol >= 30) {
                    result.error = "недопустимый код расстояния";
                    return result;
                }
                const std::size_t distance = static_cast<std::size_t>(distanceBase()[static_cast<std::size_t>(distanceSymbol)]) +
                                             reader.bits(distanceExtra()[static_cast<std::size_t>(distanceSymbol)]);
                if (distance == 0 || distance > out.size()) {
                    result.error = "ссылка на позицию за пределами распакованных данных";
                    return result;
                }
                if (out.size() + length > maxOutput) {
                    result.error = "распакованный размер превышает ожидаемый";
                    return result;
                }
                std::size_t source = out.size() - distance;
                for (std::size_t i = 0; i < length; ++i) {
                    out.push_back(out[source + i]);
                }
            }
        } else {
            result.error = "недопустимый тип блока";
            return result;
        }
        if (final != 0) {
            break;
        }
        if (reader.failed()) {
            result.error = "поток обрывается до конца блока";
            return result;
        }
    }
    result.ok = true;
    result.bytesConsumed = reader.position();
    return result;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

InflateResult zlibInflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                          std::size_t maxOutput) {
    InflateResult result;
    if (size < 6) {
        result.error = "поток zlib короче обязательных 6 байт";
        return result;
    }
    const std::uint8_t cmf = data[0];
    const std::uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8) {
        result.error = "метод сжатия zlib " + std::to_string(cmf & 0x0Fu) + " не поддерживается";
            return result;
    }
    if ((cmf >> 4) > 7) {
        result.error = "недопустимое окно zlib";
        return result;
    }
    if (((static_cast<std::uint32_t>(cmf) << 8) | flg) % 31u != 0) {
        result.error = "заголовок zlib не проходит проверку контрольного разряда";
        return result;
    }
    if ((flg & 0x20u) != 0) {
        result.error = "поток zlib использует предопределённый словарь, он не поддерживается";
            return result;
    }
    result = inflate(data + 2, size - 6, out, maxOutput);
    if (!result.ok) {
        return result;
    }
    const std::uint32_t stored = (static_cast<std::uint32_t>(data[size - 4]) << 24) |
                                 (static_cast<std::uint32_t>(data[size - 3]) << 16) |
                                 (static_cast<std::uint32_t>(data[size - 2]) << 8) |
                                 static_cast<std::uint32_t>(data[size - 1]);
    if (adler32(out.data(), out.size()) != stored) {
        result.ok = false;
        result.error = "контрольная сумма Adler-32 распакованных данных не сходится";
        return result;
    }
    result.checksumVerified = true;
    result.bytesConsumed = size;
    return result;
}

} // namespace lab2
