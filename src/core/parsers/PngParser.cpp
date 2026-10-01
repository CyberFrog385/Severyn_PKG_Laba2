#include "core/parsers/Parsers.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/ByteReader.h"
#include "core/codec/Crc32.h"
#include "core/parsers/ParserCommon.h"

namespace lab2 {

namespace {

constexpr std::size_t kHeadBytes = 64 * 1024;
constexpr int kMaxWalkSteps = 512;

const std::uint8_t kSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

std::string colorTypeName(std::uint8_t colorType) {
    switch (colorType) {
        case 0:
            return "градации серого";
        case 2:
            return "RGB (без альфа)";
        case 3:
            return "палитра";
        case 4:
            return "градация серого + альфа";
        case 6:
            return "RGBA";
        default:
            return "неизвестный тип цвета";
    }
}

int channelsOf(std::uint8_t colorType) {
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

std::string bitDepthText(std::uint8_t colorType, std::uint8_t bitDepth) {
    const int channels = channelsOf(colorType);
    if (channels == 0) {
        return text(static_cast<long long>(bitDepth));
    }
    return text(static_cast<long long>(bitDepth)) + " бит/канал x " + text(channels) + " = " +
           text(static_cast<long long>(bitDepth * channels)) + " бит/пиксель";
}

std::string asciiField(const std::uint8_t* data, std::size_t size, std::size_t limit = 90) {
    std::string out;
    for (std::size_t i = 0; i < size && i < limit; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == 0) {
            break;
        }
        out.push_back(c >= 32 && c < 127 ? c : '.');
    }
    return out;
}

std::string chunkName(const std::uint8_t* data) {
    std::string name;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>(data[i]);
        name.push_back(c >= 32 && c < 127 ? c : '.');
    }
    return name;
}

}

bool parsePng(FileHead& file, ImageRecord& record) {
    std::vector<std::uint8_t> head;
    if (!file.readAt(0, kHeadBytes, head) || head.size() < 8) {
        addProblem(record, "Файл короче 8 байт, нет сигнатуры PNG", CheckState::Corrupted);
        return false;
    }
    if (std::memcmp(head.data(), kSignature, 8) != 0) {
        addProblem(record, "Сигнатура PNG не найдена", CheckState::Corrupted);
        return false;
    }

    bool headerFound = false;
    bool iendFound = false;
    bool crcError = false;
    std::uint64_t idatBytes = 0;
    int idatChunks = 0;
    std::uint64_t firstIdatOffset = 0;
    std::uint64_t paletteEntries = 0;
    bool hasTrns = false;
    bool hasPhys = false;
    double physX = 0.0;
    double physY = 0.0;
    std::uint8_t physUnit = 0;
    std::string gamma;
    std::string srgb;
    std::vector<std::string> texts;
    std::map<std::string, int> chunkCounts;
    std::uint8_t bitDepth = 0;
    std::uint8_t colorType = 0;
    std::uint8_t compressionMethod = 0;
    std::uint8_t filterMethod = 0;
    std::uint8_t interlace = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t position = 8;

    while (position + 8 <= head.size()) {
        ByteReader reader(head.data(), head.size());
        if (!reader.seek(static_cast<std::size_t>(position))) {
            break;
        }
        const std::uint32_t length = reader.u32be();
        const std::uint8_t* type = reader.pointer();
        reader.skip(4);
        const std::uint8_t* dataPtr = head.data() + reader.pos();
        const std::size_t available = head.size() - reader.pos();
        const std::uint64_t chunkEnd = position + 12 + length;
        if (chunkEnd > file.size()) {
            addProblem(record,
                       "Чанк " + chunkName(type) + " объявлен длиной " +
                           text(static_cast<long long>(length)) + " Б и выходит за пределы файла " +
                           humanSize(file.size()),
                       CheckState::Corrupted);
            break;
        }
        if (length + 4 > available) {
            if (chunkName(type) == "IDAT") {
                firstIdatOffset = position;
                break;
            }
            if (chunkName(type) == "IEND") {
                iendFound = true;
                break;
            }
            chunkCounts[chunkName(type)] += 1;
            position = chunkEnd;
            continue;
        }
        const std::uint32_t storedCrc = (static_cast<std::uint32_t>(dataPtr[length]) << 24) |
                                        (static_cast<std::uint32_t>(dataPtr[length + 1]) << 16) |
                                        (static_cast<std::uint32_t>(dataPtr[length + 2]) << 8) |
                                        static_cast<std::uint32_t>(dataPtr[length + 3]);
        const std::uint32_t computed = crc32(type, length + 4);
        if (computed != storedCrc) {
            crcError = true;
            addProblem(record,
                       "Контрольная сумма чанка " + chunkName(type) + " не сходится (в файле " +
                           std::to_string(storedCrc) + ", вычислено " + std::to_string(computed) + ")",
                       CheckState::Corrupted);
        }
        chunkCounts[chunkName(type)] += 1;

        if (chunkName(type) == "IHDR") {
            if (length != 13) {
                addProblem(record,
                           "Чанк IHDR имеет длину " + text(static_cast<long long>(length)) +
                               " Б вместо обязательных 13 Б",
                           CheckState::Corrupted);
                return false;
            }
            width = (static_cast<std::uint32_t>(dataPtr[0]) << 24) |
                    (static_cast<std::uint32_t>(dataPtr[1]) << 16) |
                    (static_cast<std::uint32_t>(dataPtr[2]) << 8) |
                    static_cast<std::uint32_t>(dataPtr[3]);
            height = (static_cast<std::uint32_t>(dataPtr[4]) << 24) |
                     (static_cast<std::uint32_t>(dataPtr[5]) << 16) |
                     (static_cast<std::uint32_t>(dataPtr[6]) << 8) |
                     static_cast<std::uint32_t>(dataPtr[7]);
            bitDepth = dataPtr[8];
            colorType = dataPtr[9];
            compressionMethod = dataPtr[10];
            filterMethod = dataPtr[11];
            interlace = dataPtr[12];
            headerFound = true;
        } else if (chunkName(type) == "PLTE") {
            paletteEntries = length / 3;
        } else if (chunkName(type) == "tRNS") {
            hasTrns = true;
        } else if (chunkName(type) == "pHYs") {
            if (length >= 9) {
                physX = static_cast<double>((static_cast<std::uint32_t>(dataPtr[0]) << 24) |
                                            (static_cast<std::uint32_t>(dataPtr[1]) << 16) |
                                            (static_cast<std::uint32_t>(dataPtr[2]) << 8) |
                                            static_cast<std::uint32_t>(dataPtr[3]));
                physY = static_cast<double>((static_cast<std::uint32_t>(dataPtr[4]) << 24) |
                                            (static_cast<std::uint32_t>(dataPtr[5]) << 16) |
                                            (static_cast<std::uint32_t>(dataPtr[6]) << 8) |
                                            static_cast<std::uint32_t>(dataPtr[7]));
                physUnit = dataPtr[8];
                hasPhys = true;
            }
        } else if (chunkName(type) == "gAMA" && length >= 4) {
            const std::uint32_t value = (static_cast<std::uint32_t>(dataPtr[0]) << 24) |
                                        (static_cast<std::uint32_t>(dataPtr[1]) << 16) |
                                        (static_cast<std::uint32_t>(dataPtr[2]) << 8) |
                                        static_cast<std::uint32_t>(dataPtr[3]);
            gamma = number(static_cast<double>(value) / 100000.0, 4);
        } else if (chunkName(type) == "sRGB" && length >= 1) {
            static const char* intents[] = {"серый", "известные цвета", "абсолютный", "относительный"};
            const std::uint8_t intent = dataPtr[0] < 4 ? dataPtr[0] : 0;
            srgb = std::string(intents[intent]);
        } else if (chunkName(type) == "tEXt" || chunkName(type) == "zTXt") {
            if (length > 2) {
                const std::string keyword = asciiField(dataPtr, length);
                std::string value;
                if (chunkName(type) == "tEXt") {
                    for (std::size_t i = 0; i < length && i < 72; ++i) {
                        if (dataPtr[i] == 0) {
                            value = asciiField(dataPtr + i + 1, length - i - 1, 70);
                            break;
                        }
                    }
                }
                if (!keyword.empty()) {
                    texts.push_back(keyword + (value.empty() ? std::string() : ": " + value));
                }
            }
        } else if (chunkName(type) == "IDAT") {
            firstIdatOffset = position;
            break;
        } else if (chunkName(type) == "IEND") {
            iendFound = true;
            break;
        }
        position = chunkEnd;
    }

    if (!headerFound) {
        addProblem(record, "Чанк IHDR не найден — файл не является корректным PNG", CheckState::Corrupted);
        return false;
    }

    if (firstIdatOffset != 0) {
        std::uint64_t walkOffset = firstIdatOffset;
        for (int step = 0; step < kMaxWalkSteps; ++step) {
            std::vector<std::uint8_t> chunkHeader;
            if (!file.readAt(walkOffset, 8, chunkHeader) || chunkHeader.size() < 8) {
                break;
            }
            const std::uint32_t length = (static_cast<std::uint32_t>(chunkHeader[0]) << 24) |
                                         (static_cast<std::uint32_t>(chunkHeader[1]) << 16) |
                                         (static_cast<std::uint32_t>(chunkHeader[2]) << 8) |
                                         static_cast<std::uint32_t>(chunkHeader[3]);
            const std::string name = chunkName(chunkHeader.data() + 4);
            if (name == "IDAT") {
                ++idatChunks;
                idatBytes += length;
            } else if (name == "IEND") {
                iendFound = true;
                break;
            }
            const std::uint64_t next = walkOffset + 12 + length;
            if (next <= walkOffset || next + 8 > file.size()) {
                break;
            }
            walkOffset = next;
        }
    }

    if (!iendFound) {
        std::vector<std::uint8_t> tail;
        if (file.readTail(32, tail) && tail.size() >= 12) {
            for (std::size_t i = 0; i + 12 <= tail.size(); ++i) {
                if (std::memcmp(tail.data() + i + 4, "IEND", 4) == 0) {
                    iendFound = true;
                    break;
                }
            }
        }
    }

    record.width = static_cast<int>(width);
    record.height = static_cast<int>(height);
    record.geometryValid = width > 0 && height > 0;
    record.bitsPerPixel = bitDepth * channelsOf(colorType);
    record.sampleDepth = bitDepth;
    record.channels = channelsOf(colorType);
    record.colorModel = colorTypeName(colorType);
    record.compression = compressionMethod == 0
                             ? "Deflate / zlib (метод 0)"
                             : "неизвестный метод " + text(static_cast<long long>(compressionMethod));
    record.compressionCode = "PNG method " + text(static_cast<long long>(compressionMethod));

    if (hasPhys && physUnit == 1 && physX > 0 && physY > 0) {
        record.dpiX = physX * kInchPerMeter;
        record.dpiY = physY * kInchPerMeter;
        record.dpiKnown = true;
        record.resolutionUnit = "дюйм (pHYs пересчитан из м^-1)";
    } else if (hasPhys) {
        record.resolutionUnit = "не задано (pHYs: единицы = " + text(static_cast<long long>(physUnit)) + ")";
    } else {
        record.resolutionUnit = "не задано (нет чанка pHYs)";
    }

    if (width == 0 || height == 0) {
        addProblem(record,
                   "В IHDR нулевой размер: " + text(static_cast<long long>(width)) + " x " +
                       text(static_cast<long long>(height)),
                   CheckState::Corrupted);
    }
    if (interlace > 1) {
        addProblem(record,
                   "Неизвестный метод чередования: " + text(static_cast<long long>(interlace)),
                   CheckState::Warning);
    }
    if (idatChunks == 0) {
        addProblem(record, "Чанки IDAT не найдены — изображение не содержит пикселей",
                   CheckState::Corrupted);
    }
    if (!iendFound) {
        addProblem(record, "В конце файла нет чанка IEND — файл оборван или повреждён",
                   CheckState::Corrupted);
    }

    const std::uint64_t rawBytes =
        bytesForSamples(static_cast<std::uint64_t>(width) * height * record.channels, bitDepth);
    const double ratio = idatBytes > 0 ? static_cast<double>(rawBytes) / static_cast<double>(idatBytes) : 0.0;

    addField(record, "Формат", "Сигнатура", "89 50 4E 47 0D 0A 1A 0A",
             "Восемь обязательных байт в начале; при их отсутствии файл не считается PNG.");
    addField(record, "Формат", "Чанк IHDR", "13 байт, всегда первый",
             "Единственный обязательный чанк: геометрия и параметры цвета хранятся только в нём.");
    addField(record, "Геометрия", "Ширина x высота",
             text(static_cast<long long>(width)) + " x " + text(static_cast<long long>(height)),
             "Первые 8 байт данных IHDR, порядок байтов — старший первый (big-endian).");
    addField(record, "Глубина", "Бит на канал", text(static_cast<long long>(bitDepth)),
             "Поле bit depth: 1, 2, 4, 8 или 16 бит на один канал.");
    addField(record, "Глубина", "Бит на пиксель", bitDepthText(colorType, bitDepth),
             "bit depth x число каналов; число каналов задаётся типом цвета.");
    addField(record, "Глубина", "Тип цвета",
             text(static_cast<long long>(colorType)) + " — " + colorTypeName(colorType),
             "Поле colour type: 0 — серый, 2 — RGB, 3 — палитра, 4 — серый с альфой, 6 — RGBA.");
    if (paletteEntries != 0) {
        addField(record, "Глубина", "Палитра (PLTE)",
                 text(static_cast<long long>(paletteEntries)) + " записей по 3 Б (RGB)",
                 "При типе цвета 3 индексы пикселей ссылаются на эту таблицу.");
    }
    if (hasTrns) {
        addField(record, "Глубина", "Прозрачность (tRNS)", "есть",
                 "Хранит один прозрачный цвет или альфа-значение для первого элемента палитры.");
    }
    if (hasPhys) {
        addField(record, "Разрешение", "pHYs",
                 text(static_cast<long long>(physX)) + " x " + text(static_cast<long long>(physY)) +
                     " " + (physUnit == 1 ? "пикселей на метр" : "единица " + text(static_cast<long long>(physUnit))),
                 "Единственный чанк PNG с физическим размером; единица 1 — метр, 0 — неизвестно.");
    }
    addField(record, "Разрешение", "DPI",
             record.dpiKnown ? number(record.dpiX) + " x " + number(record.dpiY) + " dot/inch"
                             : "не задано",
             "dot/inch = пиксели на метр x 0.0254. В сантиметрах PNG разрешение не хранит.");
    addField(record, "Сжатие", "Метод сжатия", record.compression,
             "Поле compression method в IHDR; в PNG определён только метод 0 (zlib deflate).");
    addField(record, "Сжатие", "Фильтрация строк",
             "метод " + text(static_cast<long long>(filterMethod)) +
                 " (adaptive: None/Sub/Up/Average/Paeth, тип выбирается для каждой строки)",
             "Поле filter method в IHDR. Конкретные фильтры реально применённых строк видны "
             "только после распаковки IDAT — их показывает глубокий анализ.");
    addField(record, "Сжатие", "Данные изображения (IDAT)",
             text(static_cast<long long>(idatChunks)) + " чанк(ов), " + humanSize(idatBytes) +
                 " сжатых данных",
             "Чанки обходятся по длинам, без чтения самих данных; при большом числе чанков обход "
             "ограничен " + text(kMaxWalkSteps) + " шагами.");
    addField(record, "Сжатие", "Коэффициент сжатия", number(ratio) + " : 1",
             "Отношение размера распакованных пикселей к суммарному размеру IDAT; считается "
             "по заголовку, без распаковки.");
    addField(record, "Дополнительно", "Чередование (interlace)",
             interlace == 0
                 ? "0 — построчное"
                 : (interlace == 1
                        ? "1 — Adam7 (7 проходов уменьшенного размера)"
                        : text(static_cast<long long>(interlace))),
             "Поле interlace в IHDR: изображение может быть записано семью перемешанными проходами.");
    addField(record, "Дополнительно", "Состав чанков", [&chunkCounts] {
        std::string out;
        for (const auto& entry : chunkCounts) {
            if (!out.empty()) {
                out += ", ";
            }
            out += entry.first;
            if (entry.second > 1) {
                out += " x" + text(static_cast<long long>(entry.second));
            }
        }
        return out;
    }(),
             "Каждый чанк PNG — это блок «длина + имя + данные + CRC32»; имена перечислены в "
             "спецификации.");
    if (!srgb.empty()) {
        addField(record, "Дополнительно", "Цветовое пространство (sRGB)", srgb,
                 "Чанк sRGB задаёт известный профиль; на геометрию не влияет.");
    }
    if (!gamma.empty()) {
        addField(record, "Дополнительно", "Гамма (gAMA)", gamma,
                 "Гамма-коррекция: значение / 100000.");
    }
    for (const std::string& entry : texts) {
        addField(record, "Дополнительно", "Текстовый чанк", entry,
                 "tEXt/zTXt хранят авторство и описание прямо в файле.");
    }
    addField(record, "Проверка", "Контрольные суммы",
             crcError ? "есть несовпадения CRC32" : "все проверенные CRC32 сошлись",
             "CRC32 каждого чанка считается по алгоритму CRC-32 (полином 0xEDB88320), "
             "как требует спецификация PNG.");
    addField(record, "Проверка", "Завершающий чанк IEND",
             iendFound ? "найден" : "НЕ НАЙДЕН",
             "IEND обязан быть последним чанком; его отсутствие — признак обрезки файла.");
    addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
             "Сверяются границы чанков с размером файла, CRC32 и наличие IEND.");

    return true;
}

}