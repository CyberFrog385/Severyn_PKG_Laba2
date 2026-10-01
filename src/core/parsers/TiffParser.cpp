#include "core/parsers/Parsers.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/ByteReader.h"
#include "core/parsers/ParserCommon.h"
#include "core/parsers/TiffTags.h"

namespace lab2 {

namespace {

constexpr std::size_t kHeadBytes = 8 * 1024;
constexpr int kMaxPages = 64;
constexpr int kMaxEntriesPerPage = 4096;

struct TagValue {
    std::vector<std::uint64_t> integers;
    std::vector<double> reals;
    std::string ascii;
    bool truncated = false;

    std::uint64_t first(std::size_t index = 0) const {
        return index < integers.size() ? integers[index] : 0;
    }

    double firstReal(std::size_t index = 0) const {
        if (index < reals.size()) {
            return reals[index];
        }
        if (index < integers.size()) {
            return static_cast<double>(integers[index]);
        }
        return 0.0;
    }

    std::string display() const {
        std::string out;
        if (!ascii.empty()) {
            out = ascii;
        } else if (!integers.empty()) {
            for (std::size_t i = 0; i < integers.size() && i < 8; ++i) {
                if (!out.empty()) {
                    out += ", ";
                }
                out += lab2::text(static_cast<long long>(integers[i]));
            }
            if (integers.size() > 8) {
                out += ", …";
            }
        } else if (!reals.empty()) {
            for (std::size_t i = 0; i < reals.size() && i < 6; ++i) {
                if (!out.empty()) {
                    out += ", ";
                }
                out += number(reals[i], 4);
            }
        }
        if (truncated) {
            out += out.empty() ? "обрезано" : " (обрезано)";
        }
        return out;
    }
};

struct Entry {
    std::uint16_t tag = 0;
    std::uint16_t type = 0;
    std::uint32_t count = 0;
    TagValue value;
    std::uint64_t valueOffset = 0;
    bool offsetValid = true;
};

std::uint64_t decodeUnsigned(const std::uint8_t* data, std::size_t index, std::uint16_t type,
                             bool littleEndian, bool& ok) {
    std::uint32_t unitSize = tiffTypeSize(type);
    if (unitSize == 0) {
        ok = false;
        return 0;
    }
    ByteReader reader(data + index * unitSize, unitSize);
    switch (type) {
        case 1:
            return reader.u8();
        case 3:
            return reader.u16(littleEndian);
        case 4:
            return reader.u32(littleEndian);
        case 16:
            return reader.u64(littleEndian);
        case 18:
            return reader.u64(littleEndian);
        case 6:
            return static_cast<std::uint8_t>(reader.u8());
        case 8:
            return static_cast<std::uint16_t>(reader.u16(littleEndian));
        case 9:
            return static_cast<std::uint32_t>(reader.u32(littleEndian));
        case 17:
            return static_cast<std::uint64_t>(reader.u64(littleEndian));
        default:
            ok = false;
            return 0;
    }
}

double decodeReal(const std::uint8_t* data, std::size_t index, std::uint16_t type,
                  bool littleEndian, bool& ok) {
    ByteReader reader(data + index * 8, 8);
    if (type == 5) {
        const std::uint64_t numerator = reader.u32(littleEndian);
        const std::uint64_t denominator = reader.u32(littleEndian);
        return denominator == 0 ? 0.0 : static_cast<double>(numerator) / static_cast<double>(denominator);
    }
    if (type == 10) {
        const std::uint32_t numerator = reader.u32(littleEndian);
        const std::uint32_t denominator = reader.u32(littleEndian);
        if (denominator == 0) {
            return 0.0;
        }
        const double value = static_cast<double>(static_cast<std::int32_t>(numerator)) /
                             static_cast<double>(static_cast<std::int32_t>(denominator));
        return value;
    }
    ok = false;
    return 0.0;
}

std::string decodeText(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const char c = static_cast<char>(data[i]);
        out.push_back(c == 0 ? '\n' : (c >= 32 && c < 127 ? c : '.'));
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.size() > 160) {
        out.resize(157);
        out += "...";
    }
    return out;
}

void decodeValue(Entry& entry, const std::uint8_t* data, std::size_t size, bool littleEndian) {
    const std::uint32_t unitSize = tiffTypeSize(entry.type);
    if (unitSize == 0) {
        entry.offsetValid = false;
        return;
    }
    const std::uint64_t available = size / unitSize;
    const std::size_t count = static_cast<std::size_t>(
        std::min<std::uint64_t>(entry.count, available));
    entry.value.truncated = count != entry.count;
    if (entry.type == 2) {
        entry.value.ascii = decodeText(data, count);
        return;
    }
    entry.value.integers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        bool ok = true;
        const std::uint64_t raw = decodeUnsigned(data, i, entry.type, littleEndian, ok);
        if (!ok) {
            break;
        }
        entry.value.integers.push_back(raw);
    }
    if (entry.type == 5 || entry.type == 10 || entry.type == 11 || entry.type == 12) {
        entry.value.integers.clear();
        const std::size_t units = size / 8;
        entry.value.reals.reserve(units);
        for (std::size_t i = 0; i < units; ++i) {
            bool ok = true;
            const double value = decodeReal(data, i, entry.type, littleEndian, ok);
            if (!ok) {
                break;
            }
            entry.value.reals.push_back(value);
        }
    }
}

}

bool parseTiff(FileHead& file, ImageRecord& record) {
    std::vector<std::uint8_t> head;
    if (!file.readAt(0, kHeadBytes, head) || head.size() < 8) {
        addProblem(record, "Файл короче 8 байт, нет заголовка TIFF", CheckState::Corrupted);
        return false;
    }

    ByteReader root(head.data(), head.size());
    const std::uint16_t byteOrder = root.u16be();
    if (byteOrder == 0x4949) {
        record.signature[0] = 'I';
    } else if (byteOrder == 0x4D4D) {
        record.signature[0] = 'M';
    }
    const bool littleEndian = byteOrder == 0x4949;
    if (!littleEndian && byteOrder != 0x4D4D) {
        addProblem(record,
                   "Неизвестен порядок байтов: " + text(static_cast<long long>(byteOrder)),
                   CheckState::Corrupted);
        return false;
    }
    const std::uint16_t magic = root.u16(littleEndian);
    const bool bigTiff = magic == 43;
    if (magic != 42 && magic != 43) {
        addProblem(record,
                   "Неверный magic: " + text(static_cast<long long>(magic)) + " вместо 42 или 43",
                   CheckState::Corrupted);
        return false;
    }
    std::uint64_t ifdOffset = 0;
    if (bigTiff) {
        root.skip(2);
        root.u16(littleEndian);
        ifdOffset = root.u64(littleEndian);
    } else {
        ifdOffset = root.u32(littleEndian);
    }

    const std::size_t entrySize = bigTiff ? 20 : 12;
    const std::size_t countSize = bigTiff ? 8 : 2;
    const std::size_t offsetSize = bigTiff ? 8 : 4;
    const std::size_t inlineCapacity = bigTiff ? 8 : 4;

    std::vector<Entry> firstPage;
    std::vector<Entry> allEntries;
    std::uint64_t pixelBytes = 0;
    std::uint64_t stripBytes = 0;
    int stripCount = 0;
    int pages = 0;
    std::uint64_t colorMapEntries = 0;
    std::uint64_t offset = ifdOffset;
    bool chainBroken = false;
    bool sawOrientation = false;
    int orientation = 0;

    while (offset != 0 && pages < kMaxPages) {
        if (offset + countSize > file.size()) {
            addProblem(record,
                       "Смещение каталога IFD " + text(static_cast<long long>(offset)) +
                           " лежит за пределами файла " + humanSize(file.size()),
                       CheckState::Corrupted);
            break;
        }
        std::vector<std::uint8_t> countBlock;
        if (!file.readAt(offset, countSize, countBlock) || countBlock.size() < countSize) {
            chainBroken = true;
            break;
        }
        ByteReader countReader(countBlock.data(), countBlock.size());
        const std::uint64_t entryCount = bigTiff ? countReader.u64(littleEndian)
                                                 : countReader.u16(littleEndian);
        if (entryCount > kMaxEntriesPerPage) {
            addProblem(record,
                       "В IFD указано " + text(static_cast<long long>(entryCount)) +
                           " тегов — структура файла некорректна",
                       CheckState::Corrupted);
            break;
        }
        const std::size_t blockSize = countSize + static_cast<std::size_t>(entryCount) * entrySize +
                                      offsetSize;
        std::vector<std::uint8_t> block;
        if (!file.readAt(offset, blockSize, block) || block.size() < countSize) {
            chainBroken = true;
            break;
        }
        ++pages;
        for (std::uint64_t i = 0; i < entryCount; ++i) {
            const std::size_t entryOffset =
                countSize + static_cast<std::size_t>(i) * entrySize;
            if (entryOffset + entrySize > block.size()) {
                break;
            }
            Entry entry;
            if (bigTiff) {
                entry.tag = static_cast<std::uint16_t>(
                    ByteReader(block.data() + entryOffset, entrySize).u16(littleEndian));
                entry.type = static_cast<std::uint16_t>(
                    ByteReader(block.data() + entryOffset + 2, entrySize).u16(littleEndian));
                entry.count = 0;
            } else {
                ByteReader reader(block.data() + entryOffset, entrySize);
                entry.tag = reader.u16(littleEndian);
                entry.type = reader.u16(littleEndian);
                entry.count = reader.u32(littleEndian);
            }
            const std::size_t valuePosition = entryOffset + entrySize - inlineCapacity;
            const std::uint32_t unitSize = tiffTypeSize(entry.type);
            const std::uint64_t valueSize = unitSize != 0
                                                ? static_cast<std::uint64_t>(unitSize) * entry.count
                                                : 0;
            std::vector<std::uint8_t> valueData;
            if (unitSize == 0) {
                entry.offsetValid = false;
            } else if (valueSize <= inlineCapacity) {
                valueData.assign(block.begin() + static_cast<std::ptrdiff_t>(valuePosition),
                                 block.begin() + static_cast<std::ptrdiff_t>(valuePosition +
                                                                              static_cast<std::size_t>(valueSize)));
            } else {
                ByteReader pointer(block.data() + valuePosition, inlineCapacity);
                entry.valueOffset = bigTiff ? pointer.u64(littleEndian) : pointer.u32(littleEndian);
                if (entry.valueOffset + valueSize > file.size()) {
                    addProblem(record,
                               "Тег " + text(static_cast<long long>(entry.tag)) + " (" +
                                   (tiffTagName(entry.tag) != nullptr ? tiffTagName(entry.tag) : "без имени") +
                                   ") ссылается на данные за пределами файла",
                               CheckState::Warning);
                    entry.offsetValid = false;
                    entry.value.truncated = true;
                } else {
                    file.readAt(entry.valueOffset, static_cast<std::size_t>(valueSize), valueData);
                }
            }
            decodeValue(entry, valueData.data(), valueData.size(), littleEndian);
            allEntries.push_back(entry);
            if (pages == 1) {
                firstPage.push_back(entry);
            }
        }
        ByteReader nextReader(block.data() + countSize + static_cast<std::size_t>(entryCount) * entrySize,
                              offsetSize);
        offset = bigTiff ? nextReader.u64(littleEndian) : nextReader.u32(littleEndian);
        if (offset != 0 && offset + countSize > file.size()) {
            addProblem(record, "Смещение следующего IFD " + text(static_cast<long long>(offset)) +
                                   " выходит за пределы файла",
                       CheckState::Corrupted);
            break;
        }
    }

    auto find = [&firstPage](std::uint16_t tag) -> const Entry* {
        for (const Entry& entry : firstPage) {
            if (entry.tag == tag) {
                return &entry;
            }
        }
        return nullptr;
    };

    const Entry* widthEntry = find(256);
    const Entry* heightEntry = find(257);
    const Entry* bitsEntry = find(258);
    const Entry* compressionEntry = find(259);
    const Entry* photometricEntry = find(262);
    const Entry* samplesEntry = find(277);
    const Entry* rowsPerStripEntry = find(278);
    const Entry* stripOffsetsEntry = find(273);
    const Entry* stripCountsEntry = find(279);
    const Entry* xResolutionEntry = find(282);
    const Entry* yResolutionEntry = find(283);
    const Entry* unitEntry = find(296);
    const Entry* colorMapEntry = find(320);
    const Entry* orientationEntry = find(274);
    const Entry* extraSamplesEntry = find(338);
    const Entry* subIfdEntry = find(330);

    const std::uint64_t width = widthEntry != nullptr ? widthEntry->value.first() : 0;
    const std::uint64_t height = heightEntry != nullptr ? heightEntry->value.first() : 0;
    record.width = static_cast<int>(width);
    record.height = static_cast<int>(height);
    record.geometryValid = width > 0 && height > 0;

    std::uint64_t bitsTotal = 0;
    int sampleDepth = 0;
    std::string bitsText;
    if (bitsEntry != nullptr && !bitsEntry->value.integers.empty()) {
        for (std::size_t i = 0; i < bitsEntry->value.integers.size(); ++i) {
            bitsTotal += bitsEntry->value.integers[i];
            if (!bitsText.empty()) {
                bitsText += ", ";
            }
            bitsText += text(static_cast<long long>(bitsEntry->value.integers[i]));
        }
        sampleDepth = static_cast<int>(bitsEntry->value.integers[0]);
    }
    if (bitsTotal == 0) {
        sampleDepth = 1;
    }
    record.bitsPerPixel = static_cast<int>(bitsTotal != 0 ? bitsTotal : 1);
    record.sampleDepth = sampleDepth;
    record.channels = samplesEntry != nullptr
                          ? static_cast<int>(samplesEntry->value.first())
                          : (bitsEntry != nullptr ? static_cast<int>(bitsEntry->value.integers.size()) : 1);
    if (record.channels <= 0) {
        record.channels = 1;
    }
    if (photometricEntry != nullptr) {
        record.colorModel = tiffPhotometricName(static_cast<std::uint16_t>(photometricEntry->value.first()));
        if (extraSamplesEntry != nullptr && extraSamplesEntry->value.first() > 0) {
            record.colorModel += " + альфа";
        }
    } else {
        record.colorModel = "не определена тегом PhotometricInterpretation";
    }

    const std::uint16_t compressionCode =
        compressionEntry != nullptr ? static_cast<std::uint16_t>(compressionEntry->value.first()) : 0;
    record.compression = tiffCompressionName(compressionCode);
    record.compressionCode = "TIFF Compression = " + text(static_cast<long long>(compressionCode));

    if (unitEntry != nullptr) {
        const std::uint16_t unit = static_cast<std::uint16_t>(unitEntry->value.first());
        const double xValue = xResolutionEntry != nullptr ? xResolutionEntry->value.firstReal() : 0.0;
        const double yValue = yResolutionEntry != nullptr ? yResolutionEntry->value.firstReal() : 0.0;
        if (unit == 2 && xValue > 0 && yValue > 0) {
            record.dpiX = xValue;
            record.dpiY = yValue;
            record.dpiKnown = true;
            record.resolutionUnit = "дюйм (тег 296 = 2)";
        } else if (unit == 3 && xValue > 0 && yValue > 0) {
            record.dpiX = xValue * 2.54;
            record.dpiY = yValue * 2.54;
            record.dpiKnown = true;
            record.resolutionUnit = "сантиметр (тег 296 = 3, пересчёт x 2.54)";
        } else if (xValue > 0 && yValue > 0) {
            record.resolutionUnit =
                "не задано (тег 296 = " + text(static_cast<long long>(unit)) + ": " +
                tiffResolutionUnitName(unit) + ")";
        } else {
            record.resolutionUnit = "не задано (теги 282 и 283 отсутствуют или равны нулю)";
        }
    } else if (xResolutionEntry != nullptr && xResolutionEntry->value.firstReal() > 0) {
        record.resolutionUnit =
            "не задано (нет тега 296 ResolutionUnit: значения XResolution / YResolution "
            "считаем соотношением сторон)";
    } else {
        record.resolutionUnit = "не задано (тегов разрешения нет)";
    }

    stripCount = stripOffsetsEntry != nullptr ? static_cast<int>(stripOffsetsEntry->value.integers.size()) : 0;
    if (stripCountsEntry != nullptr) {
        for (std::uint64_t value : stripCountsEntry->value.integers) {
            stripBytes += value;
        }
    }
    if (width != 0 && height != 0) {
        pixelBytes = bytesForSamples(width * height * static_cast<std::uint64_t>(record.channels),
                                     sampleDepth);
    }

    if (width == 0 || height == 0) {
        addProblem(record,
                   "Отсутствуют теги ImageWidth (256) и ImageLength (257) либо они равны нулю",
                   CheckState::Corrupted);
    }
    if (firstPage.empty()) {
        addProblem(record, "Каталог IDF пуст — в файле нет ни одного тега", CheckState::Corrupted);
    }
    if (compressionEntry == nullptr) {
        addProblem(record, "Нет тега Compression (259) — метод сжатия неизвестен",
                   CheckState::Warning);
    }
    if (stripCountsEntry != nullptr) {
        const Entry* counts = stripCountsEntry;
        for (std::size_t i = 0; i < counts->value.integers.size(); ++i) {
            const std::uint64_t offsetValue =
                stripOffsetsEntry != nullptr && i < stripOffsetsEntry->value.integers.size()
                    ? stripOffsetsEntry->value.integers[i]
                    : 0;
            if (offsetValue != 0 && offsetValue + counts->value.integers[i] > file.size()) {
                addProblem(record,
                           "Полоса " + text(static_cast<long long>(i + 1)) + " (смещение " +
                               text(static_cast<long long>(offsetValue)) + " Б, длина " +
                               humanSize(counts->value.integers[i]) + ") выходит за пределы файла " +
                               humanSize(file.size()) + " — файл обрезан",
                           CheckState::Corrupted);
                break;
            }
        }
    } else if (stripOffsetsEntry == nullptr) {
        addProblem(record, "Нет тегов StripOffsets (273) и StripByteCounts (279)",
                   CheckState::Warning);
    }
    if (chainBroken && pages == 0) {
        addProblem(record, "Каталог IFD не читается — файл повреждён", CheckState::Corrupted);
    }
    if (chainBroken && pages > 0) {
        addProblem(record, "Цепочка каталогов IFD прервана на странице " + text(pages),
                   CheckState::Warning);
    }
    if (bigTiff) {
        addField(record, "Формат", "Вариант", "BigTIFF (magic 43)",
                 "Расширение TIFF для файлов больше 4 ГБ: смещения и количества 8 байт.");
    }
    if (colorMapEntry != nullptr) {
        colorMapEntries = colorMapEntry->value.first(2);
    }
    if (subIfdEntry != nullptr) {
        addField(record, "Дополнительно", "Вложенных IFD (тег 330)",
                 text(static_cast<long long>(subIfdEntry->value.integers.size())),
                 "SubIFDs описывают дополнительные страницы, уменьшенные изображения или "
                 "тайлы.");
    }
    if (orientationEntry != nullptr) {
        orientation = static_cast<int>(orientationEntry->value.first());
        sawOrientation = true;
    }

    addField(record, "Формат", "Порядок байтов",
             littleEndian ? "II — младший байт первым" : "MM — старший байт первым",
             "Поле 0x4949 или 0x4D4D в начале файла определяет порядок всех остальных полей.");
    addField(record, "Формат", "Magic", text(static_cast<long long>(magic)),
             "42 — классический TIFF, 43 — BigTIFF.");
    addField(record, "Формат", "Смещение первого IFD", text(static_cast<long long>(ifdOffset)) + " Б",
             "Каталог тегов хранится отдельно от данных пикселей — поэтому читается лениво.");
    addField(record, "Формат", "Страниц (IFD в цепочке)", text(pages),
             "Каждый следующий каталог указывается в поле «смещение следующего IFD» в конце "
             "предыдущего.");
    addField(record, "Формат", "Тегов в IFD0", text(static_cast<long long>(firstPage.size())),
             "Количество 12-байтных записей в первом каталоге.");
    addField(record, "Геометрия", "Ширина x высота",
             text(static_cast<long long>(width)) + " x " + text(static_cast<long long>(height)),
             "Теги 256 ImageWidth и 257 ImageLength.");
    addField(record, "Глубина", "Бит на выборку",
             bitsText.empty() ? std::string("тег 258 отсутствует") : bitsText,
             "Тег 258 BitsPerSample: по одному значению на канал, например 8,8,8 для RGB.");
    addField(record, "Глубина", "Бит на пиксель",
             text(static_cast<long long>(record.bitsPerPixel)) + " (сумма BitsPerSample)",
             "Глубина цвета равна сумме бит на все каналы.");
    addField(record, "Глубина", "Каналов (тег 277)",
             text(static_cast<long long>(samplesEntry != nullptr ? samplesEntry->value.first() : 0)),
             "SamplesPerPixel: 1 — серый, 3 — RGB, 4 — CMYK или RGB с альфой.");
    addField(record, "Глубина", "Интерпретация цвета (тег 262)",
             photometricEntry != nullptr
                 ? text(static_cast<long long>(photometricEntry->value.first())) + " — " + record.colorModel
                 : "тег отсутствует",
             "PhotometricInterpretation: 1 — чёрное ноль, 2 — RGB, 3 — палитра, 5 — CMYK, "
             "6 — YCbCr.");
    if (colorMapEntries != 0) {
        addField(record, "Глубина", "Палитра (тег 320)",
                 text(static_cast<long long>(colorMapEntries / 3)) + " цветов",
                 "Три массива по 16 бит (65536 значений) — хранят приращения компонентов.");
    }
    addField(record, "Разрешение", "XResolution / YResolution (теги 282, 283)",
             (xResolutionEntry != nullptr ? xResolutionEntry->value.display() : std::string("нет")) +
                 " / " + (yResolutionEntry != nullptr ? yResolutionEntry->value.display() : std::string("нет")),
             "Значения типа RATIONAL (числитель/знаменатель), например 72/1.");
    addField(record, "Разрешение", "Единица (тег 296)",
             unitEntry != nullptr
                 ? text(static_cast<long long>(unitEntry->value.first())) + " — " +
                       tiffResolutionUnitName(static_cast<std::uint16_t>(unitEntry->value.first()))
                 : "тег отсутствует",
             "Без тега 296 значения 282 и 283 трактуются как соотношение сторон, а не dpi.");
    addField(record, "Разрешение", "DPI",
             record.dpiKnown ? number(record.dpiX) + " x " + number(record.dpiY) + " dot/inch"
                             : "не задано",
             "dot/inch = XResolution при единице 2; при единице 3 (сантиметры) умножается на 2.54.");
    addField(record, "Сжатие", "Метод (тег 259)",
             text(static_cast<long long>(compressionCode)) + " — " + record.compression,
             "Compression: 1 — без сжатия, 5 — LZW, 8 и 32946 — Deflate, 32773 — PackBits.");
    addField(record, "Сжатие", "Полос (тег 273)",
             text(stripCount) + ", строк в полосе " +
                 (rowsPerStripEntry != nullptr
                      ? text(static_cast<long long>(rowsPerStripEntry->value.first()))
                      : std::string("не задано")),
             "StripOffsets и StripByteCounts описывают, где лежат куски изображения.");
    addField(record, "Сжатие", "Данных пикселей", humanSize(stripBytes),
             "Сумма StripByteCounts — реальный объём сжатых данных в файле.");
    if (pixelBytes > 0 && stripBytes > 0) {
        addField(record, "Сжатие", "Коэффициент сжатия",
                 number(static_cast<double>(pixelBytes) / static_cast<double>(stripBytes)) + " : 1",
                 "Ожидаемый размер пикселей (ширина x высота x бит на пиксель) делится на "
                 "объём сжатых данных.");
    }
    if (sawOrientation) {
        addField(record, "Дополнительно", "Ориентация (тег 274)",
                 text(orientation), "1 — обычная, 3 — поворот на 180, 6 — поворот на 90, "
                                    "8 — поворот на 270.");
    }
    {
        std::string tagList;
        int shown = 0;
        for (const Entry& entry : firstPage) {
            if (shown++ == 14) {
                tagList += ", …";
                break;
            }
            if (!tagList.empty()) {
                tagList += "; ";
            }
            tagList += text(static_cast<long long>(entry.tag)) + "=" + entry.value.display();
        }
        addField(record, "Дополнительно", "Теги IFD0", tagList,
                 "Для каждого тега: номер, тип, количество и значение; значения длиннее 4 байт "
                 "хранятся по смещению.");
    }
    {
        int badOffsets = 0;
        for (const Entry& entry : allEntries) {
            if (!entry.offsetValid) {
                ++badOffsets;
            }
        }
        if (badOffsets > 0) {
            addField(record, "Проверка", "Некорректные теги", text(badOffsets) + " шт.",
                     "Значения таких тегов ссылаются за пределы файла или имеют неизвестный тип.");
        }
    }
    addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
             "Проверяются границы каталогов, значений тегов и полос с фактическим размером файла.");

    return true;
}

}