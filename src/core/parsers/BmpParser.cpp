#include "core/parsers/Parsers.h"

#include <algorithm>
#include <vector>

#include "core/ByteReader.h"
#include "core/parsers/ParserCommon.h"

namespace lab2 {

namespace {

struct DibHeader {
    std::uint32_t size = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint16_t planes = 0;
    std::uint16_t bitsPerPixel = 0;
    std::uint32_t compression = 0;
    std::uint32_t imageSize = 0;
    std::int32_t xPixelsPerMeter = 0;
    std::int32_t yPixelsPerMeter = 0;
    std::uint32_t colorsUsed = 0;
    std::uint32_t colorsImportant = 0;
    std::uint32_t redMask = 0;
    std::uint32_t greenMask = 0;
    std::uint32_t blueMask = 0;
    std::uint32_t alphaMask = 0;
    bool topDown = false;
};

std::string dibVersionName(std::uint32_t size) {
    switch (size) {
        case 12:
            return "BITMAPCOREHEADER (12 байт, OS/2)";
        case 40:
            return "BITMAPINFOHEADER (40 байт)";
        case 52:
            return "BITMAPV2HEADER (52 байт)";
        case 56:
            return "BITMAPV2HEADER (56 байт, с масками)";
        case 64:
            return "BITMAPCOREHEADER2 (64 байт, OS/2)";
        case 108:
            return "BITMAPV4HEADER (108 байт)";
        case 124:
            return "BITMAPV5HEADER (124 байт)";
        default:
            return "неизвестный DIB-заголовок (" + text(static_cast<long long>(size)) + " байт)";
    }
}

std::string compressionName(std::uint32_t code) {
    switch (code) {
        case 0:
            return "Без сжатия (BI_RGB)";
        case 1:
            return "RLE8 (BI_RLE8, 8 бит с палитрой)";
        case 2:
            return "RLE4 (BI_RLE4, 4 бита с палитрой)";
        case 3:
            return "Битfields (BI_BITFIELDS)";
        case 4:
            return "JPEG (BI_JPEG)";
        case 5:
            return "PNG (BI_PNG)";
        case 6:
            return "Битfields с альфа (BI_ALPHABITFIELDS)";
        case 11:
            return "CMYK (BI_CMYK)";
        case 12:
            return "CMYK RLE8 (BI_CMYKRLE8)";
        case 13:
            return "CMYK RLE4 (BI_CMYKRLE4)";
        default:
            return "неизвестный код (" + text(static_cast<long long>(code)) + ")";
    }
}

std::string colorModelText(const DibHeader& header) {
    if (header.bitsPerPixel <= 8) {
        const std::uint32_t count = header.colorsUsed != 0
                                       ? header.colorsUsed
                                       : (1u << header.bitsPerPixel);
        return "палитра " + text(static_cast<long long>(count)) + " цветов";
    }
    switch (header.bitsPerPixel) {
        case 16:
            return header.compression == 3 ? "RGB 555 через маски битов" : "RGB 5-5-5";
        case 24:
            return "RGB 8-8-8 (без альфа)";
        case 32:
            return header.compression == 3 ? "RGB через маски битов" : "RGB 8-8-8 + 8 бит альфа";
        default:
            return "";
    }
}

void readMasks(ByteReader& reader, DibHeader& header) {
    if (header.size == 52) {
        reader.seek(24);
    } else if (header.size >= 56) {
        reader.seek(40);
    }
    if (header.size >= 56) {
        header.redMask = reader.u32le();
        header.greenMask = reader.u32le();
        header.blueMask = reader.u32le();
    }
    if (header.size >= 108) {
        header.alphaMask = reader.u32le();
    } else if (header.compression == 3 || header.compression == 6) {
        header.redMask = reader.u32le();
        header.greenMask = reader.u32le();
        header.blueMask = reader.u32le();
    }
}

std::uint32_t countBits(std::uint32_t mask) {
    std::uint32_t count = 0;
    while (mask != 0) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

}

bool parseBmp(FileHead& file, ImageRecord& record) {
    std::vector<std::uint8_t> fileHeader;
    if (!file.readAt(0, 14, fileHeader) || fileHeader.size() < 14) {
        addProblem(record, "Файл короче 14 байт, нет BITMAPFILEHEADER", CheckState::Corrupted);
        return false;
    }

    ByteReader header(fileHeader.data(), fileHeader.size());
    header.skip(2);
    const std::uint32_t declaredSize = header.u32le();
    header.skip(4);
    const std::uint32_t pixelOffset = header.u32le();

    std::vector<std::uint8_t> dibBytes;
    if (!file.readAt(14, 124, dibBytes) || dibBytes.size() < 12) {
        addProblem(record, "Нет DIB-заголовка после BITMAPFILEHEADER", CheckState::Corrupted);
        return false;
    }

    ByteReader dib(dibBytes.data(), dibBytes.size());
    DibHeader info;
    info.size = dib.u32le();
    if (info.size < 12 || info.size > 124) {
        addProblem(record,
                   "Недопустимый размер DIB-заголовка: " + text(static_cast<long long>(info.size)) +
                       " байт",
                   CheckState::Corrupted);
        return false;
    }
    if (info.size == 12) {
        info.width = dib.u16le();
        info.height = dib.u16le();
        info.planes = dib.u16le();
        info.bitsPerPixel = dib.u16le();
    } else {
        info.width = static_cast<std::int32_t>(dib.u32le());
        info.height = static_cast<std::int32_t>(dib.u32le());
        info.topDown = info.height < 0;
        info.planes = dib.u16le();
        info.bitsPerPixel = dib.u16le();
        info.compression = dib.u32le();
        info.imageSize = dib.u32le();
        info.xPixelsPerMeter = static_cast<std::int32_t>(dib.u32le());
        info.yPixelsPerMeter = static_cast<std::int32_t>(dib.u32le());
        info.colorsUsed = dib.u32le();
        info.colorsImportant = dib.u32le();
        readMasks(dib, info);
    }

    record.width = info.width;
    record.height = info.topDown ? info.height : std::abs(info.height);
    record.geometryValid = record.width > 0 && record.height > 0;
    record.bitsPerPixel = info.bitsPerPixel;
    record.channels = info.bitsPerPixel >= 24 ? 3 : 1;
    record.sampleDepth = info.bitsPerPixel >= 8 ? 8 : info.bitsPerPixel;
    record.colorModel = colorModelText(info);
    record.compression = compressionName(info.compression);
    record.compressionCode = "BI_" + text(static_cast<long long>(info.compression));

    if (!record.geometryValid) {
        addProblem(record,
                   "Недопустимая геометрия: ширина " + text(record.width) + ", высота " +
                       text(record.height),
                   CheckState::Corrupted);
    }
    if (info.planes != 1 && info.planes != 0) {
        addProblem(record,
                   "Число плоскостей biPlanes = " + text(static_cast<long long>(info.planes)) +
                       ", ожидается 1",
                   CheckState::Warning);
    }
    if (info.bitsPerPixel != 1 && info.bitsPerPixel != 2 && info.bitsPerPixel != 4 &&
        info.bitsPerPixel != 8 && info.bitsPerPixel != 16 && info.bitsPerPixel != 24 &&
        info.bitsPerPixel != 32) {
        addProblem(record,
                   "Нестандартная глубина biBitCount = " +
                       text(static_cast<long long>(info.bitsPerPixel)) + " бит",
                   CheckState::Warning);
    }

    if (info.xPixelsPerMeter > 0 && info.yPixelsPerMeter > 0) {
        record.dpiX = info.xPixelsPerMeter * kInchPerMeter;
        record.dpiY = info.yPixelsPerMeter * kInchPerMeter;
        record.dpiKnown = true;
        record.resolutionUnit = "дюйм";
    } else {
        record.dpiX = 0.0;
        record.dpiY = 0.0;
        record.dpiKnown = false;
        record.resolutionUnit = "не задано в файле";
    }

    std::uint32_t paletteCount = 0;
    std::uint32_t paletteEntrySize = 3;
    std::uint32_t paletteOffset = 14 + info.size;
    if (info.size != 12 && info.bitsPerPixel <= 8) {
        paletteEntrySize = 4;
        paletteCount = info.colorsUsed != 0 ? info.colorsUsed : (1u << info.bitsPerPixel);
    } else if (info.size == 12 && info.bitsPerPixel <= 8) {
        paletteCount = 1u << info.bitsPerPixel;
    }
    if (paletteCount > 256) {
        paletteCount = 256;
    }

    std::uint32_t paletteDistinct = 0;
    bool paletteGray = true;
    if (paletteCount != 0) {
        std::vector<std::uint8_t> palette;
        if (file.readAt(paletteOffset, paletteCount * paletteEntrySize, palette) &&
            palette.size() == paletteCount * paletteEntrySize) {
            std::vector<std::uint32_t> seen;
            seen.reserve(paletteCount);
            for (std::uint32_t i = 0; i < paletteCount; ++i) {
                const std::uint32_t blue = palette[i * paletteEntrySize + 0];
                const std::uint32_t green = palette[i * paletteEntrySize + 1];
                const std::uint32_t red = palette[i * paletteEntrySize + 2];
                const std::uint32_t rgb = (red << 16) | (green << 8) | blue;
                if (std::find(seen.begin(), seen.end(), rgb) == seen.end()) {
                    seen.push_back(rgb);
                }
                if (red != green || green != blue) {
                    paletteGray = false;
                }
            }
            paletteDistinct = static_cast<std::uint32_t>(seen.size());
        } else {
            addProblem(record,
                       "Палитра (" + text(static_cast<long long>(paletteCount)) +
                           " записей) обрезана: файл короче " +
                           text(static_cast<long long>(paletteOffset + paletteCount * paletteEntrySize)) +
                           " байт",
                       CheckState::Corrupted);
        }
    }

    if (declaredSize != 0 && declaredSize > file.size()) {
        addProblem(record,
                   "Заголовок bfSize = " + humanSize(declaredSize) + ", а файл весит " +
                       humanSize(file.size()) + " — файл обрезан",
                   CheckState::Corrupted);
    } else if (declaredSize != 0 && declaredSize + 1024 < file.size()) {
        addProblem(record,
                   "bfSize = " + humanSize(declaredSize) + " заметно меньше размера файла " +
                       humanSize(file.size()) + " — возможно дописан хвост",
                   CheckState::Warning);
    }
    if (pixelOffset > file.size()) {
        addProblem(record,
                   "Смещение данных пикселей bfOffBits = " +
                       text(static_cast<long long>(pixelOffset)) + " больше размера файла " +
                       humanSize(file.size()),
                   CheckState::Corrupted);
    }

    const bool rle = info.compression == 1 || info.compression == 2;
    if (rle) {
        addField(record, "Сжатие", "Оценка размера",
                 "сжатие RLE, точная длина данных не выводится из заголовка",
                 "При RLE размер вычисляется по потоку кодов, поэтому в заголовке его нет; "
                 "файл читается целиком только при глубоком анализе.");
    } else if (record.geometryValid && info.bitsPerPixel != 0) {
        const std::uint64_t stride = bmpRowStride(record.width, info.bitsPerPixel);
        const std::uint64_t expected = stride * static_cast<std::uint64_t>(record.height);
        const std::uint64_t available = file.size() > pixelOffset ? file.size() - pixelOffset : 0;
        if (expected > available) {
            addProblem(record,
                       "Данных пикселей не хватает: заголовок требует " + humanSize(expected) +
                           " (размер строки " + text(static_cast<long long>(stride)) + " Б x " +
                           text(record.height) + " строк), доступно " + humanSize(available),
                       CheckState::Corrupted);
        } else if (info.imageSize != 0 && info.imageSize + 512 < available) {
            addProblem(record,
                       "biSizeImage = " + humanSize(info.imageSize) + " меньше доступных данных " +
                           humanSize(available),
                       CheckState::Warning);
        }
        record.compressionCode = record.compression;
        addField(record, "Сжатие", "Размер строки",
                 text(static_cast<long long>(stride)) + " Б = (((" + text(record.width) + " x " +
                     text(static_cast<long long>(info.bitsPerPixel)) + " + 31) / 32) x 4)",
                 "Строки BMP выровнены по 4 байта — это часть формата, а не свойство файла.");
    }

    if (info.compression == 3 || info.compression == 6) {
        addField(record, "Цвет", "Маски каналов",
                 "R=" + text(static_cast<long long>(countBits(info.redMask))) + " бит, G=" +
                     text(static_cast<long long>(countBits(info.greenMask))) + " бит, B=" +
                     text(static_cast<long long>(countBits(info.blueMask))) + " бит, A=" +
                     text(static_cast<long long>(countBits(info.alphaMask))) + " бит",
                 "BI_BITFIELDS: в 16/32 битном пикселе положение каналов задаётся масками, "
                 "а не фиксированным порядком.");
    }

    addField(record, "Формат", "Сигнатура", "42 4D (символ 'BM')",
             "Первые два байта файла. Проверяются всегда, независимо от расширения.");
    addField(record, "Формат", "Размер BITMAPFILEHEADER", "14 байт",
             "Фиксированная часть заголовка: тип, размер, резерв, смещение данных пикселей.");
    addField(record, "Формат", "DIB-заголовок", dibVersionName(info.size),
             "Сразу после BITMAPFILEHEADER; определяет версию и состав полей.");
    addField(record, "Формат", "Объявленный размер файла",
             declaredSize != 0 ? humanSize(declaredSize) : "0 (не задан)",
             "Поле bfSize; сравнивается с фактическим размером файла для поиска обрезки.");
    addField(record, "Формат", "Смещение данных пикселей", humanSize(pixelOffset),
             "Поле bfOffBits: начало самих пикселей, после заголовков и палитры.");
    addField(record, "Геометрия", "Ширина x высота",
             text(record.width) + " x " + text(record.height),
             "Поля biWidth и biHeight. Отрицательный biHeight означает строки сверху вниз.");
    addField(record, "Геометрия", "Направление строк",
             info.topDown ? "сверху вниз (top-down)" : "снизу вверх (bottom-up)",
             "Знак biHeight; на отображение не влияет, но важен при ручном декодировании.");
    addField(record, "Глубина", "Бит на пиксель",
             text(static_cast<long long>(info.bitsPerPixel)),
             "Поле biBitCount: сколько бит отведено на один пиксель целиком.");
    addField(record, "Глубина", "Бит на канал",
             text(static_cast<long long>(record.sampleDepth)),
             "Глубина одного цветового канала: 8 для 16/24/32 бит, иначе равна битам на пиксель.");
    addField(record, "Глубина", "Модель цвета", record.colorModel,
             "Определяется по biBitCount и biCompression; при 8 бит и меньше цвет берётся из палитры.");
    if (paletteCount != 0) {
        addField(record, "Глубина", "Палитра",
                 text(static_cast<long long>(paletteCount)) + " записей по " +
                     text(static_cast<long long>(paletteEntrySize)) + " Б, различных цветов " +
                     text(static_cast<long long>(paletteDistinct)) +
                     (paletteGray ? ", градация серого" : ", цветная"),
                 "Палитра — таблица индексов цветов сразу после DIB-заголовка; при 24/32 битах "
                 "её нет, цвет лежит прямо в пикселе.");
    }
    if (info.colorsImportant != 0) {
        addField(record, "Глубина", "Важных цветов (biClrImportant)",
                 text(static_cast<long long>(info.colorsImportant)),
                 "Сколько цветов палитры реально используется; 0 — все.");
    }
    addField(record, "Разрешение", "Пикселей на метр",
             text(static_cast<long long>(info.xPixelsPerMeter)) + " x " +
                 text(static_cast<long long>(info.yPixelsPerMeter)),
             "Поля biXPelsPerMeter и biYPelsPerMeter — единственное место в BMP, где хранится "
             "разрешение.");
    addField(record, "Разрешение", "DPI",
             record.dpiKnown ? number(record.dpiX) + " x " + number(record.dpiY) + " dot/inch"
                             : "не задано (в заголовке нули)",
             "Пересчёт: dot/inch = пикселей на метр x 0.0254. Нулевые значения означают, что "
             "разрешение не задано.");
    addField(record, "Сжатие", "Тип сжатия", record.compression,
             "Поле biCompression: перечисляет способ хранения пикселей и код, записанный в файле.");
    if (info.imageSize != 0) {
        addField(record, "Сжатие", "Размер картинки (biSizeImage)", humanSize(info.imageSize),
                 "Сколько байт занимают пиксели; при несжатом BMP равно stride x height.");
    }
    addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
             "Сравнение объявленных размеров и смещений с фактическим размером файла.");

    return true;
}

}