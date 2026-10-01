#include "core/parsers/Parsers.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "core/ByteReader.h"
#include "core/ScanWindow.h"
#include "core/parsers/ParserCommon.h"

namespace lab2 {

namespace {

constexpr std::size_t kBlockBytes = 32 * 1024;
constexpr std::size_t kMaxHeaderBytes = 1024 * 1024;

std::string hexText(std::uint8_t value) {
    static const char* digits = "0123456789ABCDEF";
    std::string out = "FF";
    out.push_back(digits[value >> 4]);
    out.push_back(digits[value & 0x0F]);
    return out;
}

bool isStandalone(std::uint8_t marker) {
    return marker == 0x01 || marker == 0xD8 || marker == 0xFF ||
           (marker >= 0xD0 && marker <= 0xD7);
}

std::string sofName(std::uint8_t marker) {
    switch (marker) {
        case 0xC0:
            return "SOF0 — базовая последовательная DCT с кодами Хаффмана";
        case 0xC1:
            return "SOF1 — расширенная последовательная DCT";
        case 0xC2:
            return "SOF2 — прогрессивная DCT с кодами Хаффмана";
        case 0xC3:
            return "SOF3 — DCT без потерь (предсказание)";
        case 0xC5:
            return "SOF5 — дифференциальная последовательная DCT";
        case 0xC6:
            return "SOF6 — дифференциальная прогрессивная DCT";
        case 0xC7:
            return "SOF7 — дифференциальная DCT без потерь";
        case 0xC8:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            return "арифметическое кодирование (DCT без Хаффмана)";
        default:
            return "неизвестный SOF";
    }
}

const std::array<int, 64>& zigzagToRaster() {
    static const std::array<int, 64> table{0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13,
                                           16, 26, 29, 42, 3,  8,  12, 17, 25, 30, 41, 43,
                                           9,  11, 18, 24, 31, 40, 44, 53, 10, 19, 23, 32,
                                           39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
                                           21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49,
                                           57, 58, 62, 63};
    return table;
}

std::string samplingText(const std::vector<int>& h, const std::vector<int>& v) {
    if (h.size() == 3 && h[0] == 2 && h[1] == 1 && h[2] == 1) {
        return v[0] == 2 ? "4:2:0" : "4:2:2";
    }
    if (h.size() == 3 && h[0] == 1 && h[1] == 1 && h[2] == 1) {
        return "4:4:4 (без субдискретизации)";
    }
    std::string out;
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (!out.empty()) {
            out += ", ";
        }
        out += text(h[i]) + "x" + text(v[i]);
    }
    return out.empty() ? std::string("не определена") : out;
}

std::string orientationText(std::uint16_t value) {
    switch (value) {
        case 1:
            return "1 — обычная";
        case 2:
            return "2 — зеркально по горизонтали";
        case 3:
            return "3 — поворот на 180";
        case 4:
            return "4 — зеркально по вертикали";
        case 5:
            return "5 — поворот на 90 и зеркало";
        case 6:
            return "6 — поворот на 90";
        case 7:
            return "7 — поворот на 270 и зеркало";
        case 8:
            return "8 — поворот на 270";
        default:
            return text(static_cast<long long>(value));
    }
}

bool readExifOrientation(const std::uint8_t* data, std::size_t size, std::uint16_t& orientation) {
    if (size < 14) {
        return false;
    }
    const bool littleEndian = data[0] == 'I' && data[1] == 'I';
    if (!littleEndian && !(data[0] == 'M' && data[1] == 'M')) {
        return false;
    }
    ByteReader tiff(data + 6, size - 6);
    if (tiff.u16(littleEndian) != 42) {
        return false;
    }
    const std::uint32_t offset = tiff.u32(littleEndian);
    if (offset > size || !tiff.seek(offset)) {
        return false;
    }
    const std::uint16_t entries = tiff.u16(littleEndian);
    for (std::uint16_t i = 0; i < entries; ++i) {
        if (!tiff.has(12)) {
            return false;
        }
        const std::uint16_t tag = tiff.u16(littleEndian);
        const std::uint16_t fieldType = tiff.u16(littleEndian);
        tiff.u32(littleEndian);
        const std::uint32_t count = tiff.u32(littleEndian);
        if (tag == 0x0112 && count >= 1) {
            orientation = fieldType == 3
                              ? tiff.u16(littleEndian)
                              : static_cast<std::uint16_t>(tiff.u32(littleEndian));
            return orientation != 0;
        }
    }
    return false;
}

}

bool parseJpeg(FileHead& file, ImageRecord& record) {
    std::vector<std::uint8_t> start;
    if (!file.readAt(0, 4, start) || start.size() < 2) {
        addProblem(record, "Файл короче 2 байт, нет маркера SOI", CheckState::Corrupted);
        return false;
    }
    if (start[0] != 0xFF || start[1] != 0xD8) {
        addProblem(record, "Файл не начинается с маркера SOI (FF D8)", CheckState::Corrupted);
        return false;
    }

    ScanWindow window(file, kBlockBytes, kMaxHeaderBytes);
    bool sofFound = false;
    bool sosFound = false;
    bool jfifFound = false;
    bool exifFound = false;
    bool adobeFound = false;
    std::uint8_t densityUnits = 0;
    std::uint16_t jfifX = 0;
    std::uint16_t jfifY = 0;
    std::uint8_t precision = 0;
    std::uint8_t sofMarker = 0;
    int width = 0;
    int height = 0;
    int components = 0;
    int scanComponents = 0;
    std::vector<int> samplingH;
    std::vector<int> samplingV;
    std::vector<int> componentIds;
    int quantizationTables = 0;
    int huffmanTables = 0;
    int restartInterval = 0;
    std::uint16_t exifOrientationValue = 0;
    bool exifOrientationKnown = false;
    std::string iccProfile;
    std::string comment;
    std::array<std::uint16_t, 64> firstQuantTable{};
    bool firstQuantCaptured = false;
    int strayBytes = 0;

    while (true) {
        if (!window.ensure(1)) {
            break;
        }
        if (!window.atMarkerByte()) {
            if (++strayBytes > 4096) {
                break;
            }
            window.skip(1);
            continue;
        }
        strayBytes = 0;
        window.skip(1);
        while (window.ensure(1) && window.peek() == 0xFF) {
            window.skip(1);
        }
        if (!window.ensure(1)) {
            break;
        }
        const std::uint8_t marker = window.take();
        if (marker == 0xD9) {
            break;
        }
        if (isStandalone(marker) || marker == 0x00) {
            continue;
        }
        if (!window.ensure(2)) {
            break;
        }
        const std::size_t segmentLength =
            (static_cast<std::size_t>(window.take()) << 8) | window.take();
        if (segmentLength < 2) {
            addProblem(record,
                       "Некорректная длина сегмента маркера " + hexText(marker) + ": " +
                           text(static_cast<long long>(segmentLength)) + " Б",
                       CheckState::Warning);
            break;
        }
        const std::size_t payload = segmentLength - 2;
        if (!window.ensure(payload)) {
            addProblem(record,
                       "Сегмент маркера " + hexText(marker) + " объявлен длиной " +
                           text(static_cast<long long>(segmentLength)) +
                           " Б, но файла не хватает — файл обрезан",
                       CheckState::Corrupted);
            break;
        }
        ByteReader segment(window.cursor(), payload);
        window.skip(payload);

        if (marker >= 0xE0 && marker <= 0xEF) {
            const std::string appId = segment.rawString(5);
            if (appId.compare(0, 4, "JFIF") == 0) {
                jfifFound = true;
                if (segment.skip(3)) {
                    densityUnits = segment.u8();
                    jfifX = segment.u16be();
                    jfifY = segment.u16be();
                }
            } else if (appId.compare(0, 4, "Exif") == 0) {
                exifFound = true;
                if (segment.skip(2)) {
                    exifOrientationKnown =
                        readExifOrientation(segment.pointer(), segment.remaining(), exifOrientationValue);
                }
            } else if (appId.compare(0, 4, "ICC_") == 0) {
                if (segment.skip(9)) {
                    iccProfile = humanSize(segment.u32be());
                }
            } else if (appId.compare(0, 5, "Adobe") == 0) {
                adobeFound = true;
            }
        } else if (marker == 0xDB) {
            while (segment.ok() && segment.remaining() > 1) {
                const std::uint8_t info = segment.u8();
                const int precisionBits = info >> 4;
                for (int i = 0; i < 64; ++i) {
                    const std::uint16_t value = precisionBits == 0 ? segment.u8() : segment.u16be();
                    if (quantizationTables == 0) {
                        firstQuantTable[static_cast<std::size_t>(i)] = value;
                        firstQuantCaptured = true;
                    }
                }
                if (!segment.ok()) {
                    break;
                }
                ++quantizationTables;
            }
        } else if (marker == 0xC4) {
            while (segment.ok() && segment.remaining() > 0) {
                segment.u8();
                const std::uint8_t counts = segment.u8();
                if (!segment.skip(16 + counts)) {
                    break;
                }
                ++huffmanTables;
            }
        } else if (marker == 0xDD) {
            restartInterval = static_cast<int>(segment.u16be());
        } else if (marker == 0xFE) {
            if (comment.empty() && segment.remaining() > 0) {
                comment = segment.fixedString(std::min<std::size_t>(segment.remaining(), 120));
            }
        } else if (marker == 0xDA) {
            sosFound = true;
            scanComponents = segment.u8();
            break;
        } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
                   marker != 0xCC) {
            sofFound = true;
            sofMarker = marker;
            precision = segment.u8();
            height = static_cast<int>(segment.u16be());
            width = static_cast<int>(segment.u16be());
            components = segment.u8();
            samplingH.clear();
            samplingV.clear();
            componentIds.clear();
            for (int i = 0; i < components && segment.ok(); ++i) {
                componentIds.push_back(segment.u8());
                const std::uint8_t sampling = segment.u8();
                samplingH.push_back(sampling >> 4);
                samplingV.push_back(sampling & 0x0F);
                segment.u8();
            }
            if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xC3 ||
                marker == 0xC7 || marker >= 0xC9) {
                break;
            }
        }
    }

    if (!sosFound && sofFound) {
        while (window.ensure(2)) {
            if (!window.atMarkerByte()) {
                window.skip(1);
                continue;
            }
            window.skip(1);
            while (window.ensure(1) && window.peek() == 0xFF) {
                window.skip(1);
            }
            if (!window.ensure(1)) {
                break;
            }
            if (window.peek() != 0xDA) {
                window.skip(1);
                continue;
            }
            window.skip(1);
            if (window.ensure(2)) {
                const std::size_t length =
                    (static_cast<std::size_t>(window.take()) << 8) | window.take();
                const std::size_t payload = length >= 2 ? length - 2 : 0;
                if (window.ensure(payload)) {
                    ByteReader scan(window.cursor(), payload);
                    sosFound = true;
                    scanComponents = scan.u8();
                }
            }
            break;
        }
    }

    bool eoiFound = false;
    std::vector<std::uint8_t> tail;
    if (file.readTail(32, tail)) {
        if (tail.size() >= 2 && tail[tail.size() - 2] == 0xFF && tail[tail.size() - 1] == 0xD9) {
            eoiFound = true;
        } else {
            for (std::size_t i = 0; i + 1 < tail.size(); ++i) {
                if (tail[i] == 0xFF && tail[i + 1] == 0xD9) {
                    eoiFound = true;
                    break;
                }
            }
        }
    }

    record.width = width;
    record.height = height;
    record.geometryValid = width > 0 && height > 0;
    record.bitsPerPixel = precision * std::max(components, 0);
    record.sampleDepth = precision;
    record.channels = components;
    if (components == 1) {
        record.colorModel = "градации серого";
    } else if (components == 3) {
        record.colorModel = "YCbCr (при декодировании переводится в RGB)";
    } else if (components == 4) {
        record.colorModel = "CMYK или YCCK (4 компоненты)";
    } else {
        record.colorModel = components > 0 ? text(components) + " компонент(ы)" : "не определена";
    }

    record.compression = sofFound ? sofName(sofMarker) : "SOF не найден, метод неизвестен";
    if (jfifFound) {
        record.compression += ", контейнер JFIF";
    }
    record.compressionCode = sofFound ? ("SOF" + hexText(sofMarker).substr(2)) : "нет SOF";

    if (jfifFound && densityUnits != 0 && jfifX > 0 && jfifY > 0) {
        if (densityUnits == 1) {
            record.dpiX = jfifX;
            record.dpiY = jfifY;
            record.resolutionUnit = "дюйм (JFIF, density units = 1)";
        } else if (densityUnits == 2) {
            record.dpiX = jfifX * 2.54;
            record.dpiY = jfifY * 2.54;
            record.resolutionUnit = "сантиметр (JFIF, density units = 2, пересчёт x 2.54)";
        }
        record.dpiKnown = true;
    } else if (jfifFound && (jfifX == 0 || jfifY == 0)) {
        record.resolutionUnit = "не задано (в JFIF-заголовке нули)";
    } else if (jfifFound) {
        record.resolutionUnit = "не задано (JFIF density units = 0, задано только соотношение сторон)";
    } else {
        record.resolutionUnit = "не задано (нет сегмента APP0 JFIF)";
    }

    if (!sofFound) {
        addProblem(record, "Не найден маркер SOF — размеры и глубина цвета неизвестны",
                   CheckState::Corrupted);
    } else if (!record.geometryValid) {
        addProblem(record,
                   "В SOF указаны нулевые размеры: " + text(width) + " x " + text(height),
                   CheckState::Corrupted);
    }
    if (!sosFound) {
        addProblem(record, "Не найден маркер SOS — начало сжатых данных не обнаружено",
                   CheckState::Corrupted);
    }
    if (!eoiFound) {
        addProblem(record, "В конце файла нет маркера EOI (FF D9) — файл оборван",
                   CheckState::Corrupted);
    }
    if (components > 4) {
        addProblem(record,
                   "Число компонент " + text(components) + " больше 4 — нестандартный файл",
                   CheckState::Warning);
    }
    if (file.size() < 100) {
        addProblem(record, "Файл меньше 100 байт — полноценное JPEG-изображение невозможно",
                   CheckState::Warning);
    }
    if (precision != 0 && precision != 8 && precision != 12 && precision != 16) {
        addProblem(record, "Нестандартная точность выборки: " + text(static_cast<long long>(precision)) + " бит",
                   CheckState::Warning);
    }

    addField(record, "Формат", "Сигнатура", "FF D8 (SOI) в начале, FF D9 (EOI) в конце",
             "JPEG состоит из последовательности маркеров FF xx; значимые маркеры содержат "
             "двухбайтовую длину сегмента.");
    addField(record, "Формат", "Структура",
             "APPn (JFIF/EXIF/ICC) -> DQT -> SOF -> DHT -> SOS -> сжатые данные -> EOI",
             "Порядок сегментов задан спецификацией; обход идёт по маркерам, без декодирования.");
    addField(record, "Формат", "Кодек", record.compression,
             "Маркер SOF определяет метод сжатия: SOF0 — базовая DCT с кодами Хаффмана, "
             "SOF2 — прогрессивная.");
    addField(record, "Формат", "Прочитано из заголовка", humanSize(file.bytesRead()),
             "Читается только начало файла до маркера SOS; сами сжатые пиксели не загружаются.");
    addField(record, "Геометрия", "Ширина x высота", text(width) + " x " + text(height),
             "Поля маркера SOF записаны в порядке: точность, высота, ширина.");
    addField(record, "Глубина", "Точность выборки", text(static_cast<long long>(precision)) + " бит",
             "Поле precision в SOF: бит на один компонент, обычно 8.");
    addField(record, "Глубина", "Бит на пиксель",
             text(static_cast<long long>(precision)) + " x " + text(components) + " = " +
                 text(static_cast<long long>(precision * components)),
             "Глубина цвета JPEG = точность выборки x число компонент.");
    addField(record, "Глубина", "Модель цвета", record.colorModel,
             "Одна компонента — серый, три — YCbCr, четыре — CMYK или YCCK.");
    addField(record, "Глубина", "Компоненты",
             [&] {
                 std::string out;
                 for (std::size_t i = 0; i < componentIds.size(); ++i) {
                     if (!out.empty()) {
                         out += ", ";
                     }
                     out += "ID " + text(componentIds[i]);
                     if (i < samplingH.size()) {
                         out += ", семплинг " + text(samplingH[i]) + "x" + text(samplingV[i]);
                     }
                 }
                 return out.empty() ? std::string("не определены") : out;
             }(),
             "В SOF для каждого компонента: идентификатор, коэффициент семплинга по X и Y, "
             "номер таблицы квантования.");
    addField(record, "Разрешение", "JFIF (APP0)",
             jfifFound ? (text(static_cast<long long>(jfifX)) + " x " +
                          text(static_cast<long long>(jfifY)) + ", единицы " +
                          text(static_cast<long long>(densityUnits)))
                       : "сегмент отсутствует",
             "JFIF хранит Xdensity и Ydensity; единицы: 1 — dot/inch, 2 — dot/cm, 0 — только "
             "соотношение сторон пикселя.");
    addField(record, "Разрешение", "DPI",
             record.dpiKnown ? number(record.dpiX) + " x " + number(record.dpiY) + " dot/inch"
                             : "не задано",
             "При единицах 2 значение умножается на 2.54; при отсутствии JFIF разрешение "
             "не определено.");
    if (exifOrientationKnown) {
        addField(record, "Дополнительно", "Ориентация EXIF", orientationText(exifOrientationValue),
                 "Тег 0x0112 из IFD, вложенного в сегмент APP1 EXIF.");
    } else if (exifFound) {
        addField(record, "Дополнительно", "Сегмент APP1 EXIF", "есть",
                 "Расширенные метаданные камеры; ориентация в нём не найдена.");
    }
    addField(record, "Сжатие", "Таблиц квантования (DQT)",
             text(quantizationTables) + " шт." +
                 (firstQuantCaptured ? ", первая приведена ниже" : ""),
             "Таблицы задают, насколько теряются высокие частоты в каждом блоке 8x8.");
    if (firstQuantCaptured) {
        const auto& order = zigzagToRaster();
        std::array<std::uint16_t, 64> raster{};
        for (std::size_t i = 0; i < 64; ++i) {
            raster[static_cast<std::size_t>(order[i])] = firstQuantTable[i];
        }
        std::string grid;
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                grid += text(static_cast<long long>(raster[static_cast<std::size_t>(row * 8 + column)]));
                grid += column == 7 ? '\n' : ' ';
            }
        }
        addField(record, "Сжатие", "Матрица квантования (естественный порядок)", grid,
                 "Значения хранятся в порядке зигзага, здесь переставлены по строкам блока 8x8; "
                 "большие коэффициенты в правом нижнем углу отвечают за высокие частоты.");
    }
    addField(record, "Сжатие", "Таблиц Хаффмана (DHT)", text(huffmanTables) + " шт.",
             "Каждая DHT хранит 16 байт длин кодов и сами коды; по ним строится дерево "
             "декодирования.");
    addField(record, "Сжатие", "Интервал рестарта (DRI)",
             restartInterval > 0 ? text(restartInterval) + " MCU" : "не задан",
             "Каждые N MCU вставляется маркер рестарта, ограничивающий распространение ошибок.");
    addField(record, "Сжатие", "Субдискретизация", samplingText(samplingH, samplingV),
             "Коэффициенты семплинга из SOF: чем меньше семплинг канала, тем сильнее он сжимается.");
    if (scanComponents > 0) {
        addField(record, "Сжатие", "Компонент в сканировании", text(scanComponents),
                 "В маркере SOS перечислены компоненты одного прохода сканирования.");
    }
    if (!iccProfile.empty()) {
        addField(record, "Дополнительно", "ICC-профиль", iccProfile,
                 "Сегмент APP2 ICC_PROFILE: встроенный цветовой профиль.");
    }
    if (adobeFound) {
        addField(record, "Дополнительно", "Сегмент Adobe", "есть",
                 "APP13 Adobe задаёт преобразование цветов для CMYK и YCCK.");
    }
    if (!comment.empty()) {
        addField(record, "Дополнительно", "Комментарий COM", comment,
                 "Произвольный текст в сегменте FF FE.");
    }
    addField(record, "Проверка", "Завершающий маркер EOI",
             eoiFound ? "найден (FF D9)" : "НЕ НАЙДЕН",
             "EOI обязателен; его отсутствие означает, что файл оборван.");
    addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
             "Проверяются наличие SOF, SOS, EOI и ненулевая геометрия.");

    return true;
}

}