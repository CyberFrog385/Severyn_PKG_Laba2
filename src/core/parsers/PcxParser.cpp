#include "core/parsers/Parsers.h"

#include <algorithm>
#include <vector>

#include "core/ByteReader.h"
#include "core/parsers/ParserCommon.h"

namespace lab2 {

namespace {

std::string versionText(std::uint8_t version) {
    switch (version) {
        case 0:
            return "0 — PC Paintbrush 2.5";
        case 2:
            return "2 — PC Paintbrush 2.8 с палитрой EGA";
        case 3:
            return "3 — PC Paintbrush 2.8 без палитры";
        case 4:
            return "4 — Paintbrush for Windows";
        case 5:
            return "5 — Paintbrush for Windows 3.0 и новее, 256 цветов";
        default:
            return "неизвестная версия (" + text(static_cast<long long>(version)) + ")";
    }
}

std::string pictureTypeText(int channels) {
    switch (channels) {
        case 1:
            return "одноканальное (чёрно-белое или градации серого)";
        case 3:
            return "трёхканальное RGB (по 8 бит на канал)";
        case 4:
            return "четырёхканальное RGBA или 256 цветов";
        default:
            return text(channels) + " каналов";
    }
}

}

bool parsePcx(FileHead& file, ImageRecord& record) {
    std::vector<std::uint8_t> headerBytes;
    if (!file.readAt(0, 128, headerBytes) || headerBytes.size() < 128) {
        addProblem(record, "Файл короче 128 байт — заголовок PCX не помещается",
                   CheckState::Corrupted);
        return false;
    }

    ByteReader header(headerBytes.data(), headerBytes.size());
    const std::uint8_t manufacturer = header.u8();
    const std::uint8_t version = header.u8();
    const std::uint8_t encoding = header.u8();
    const std::uint8_t bitsPerPixel = header.u8();
    const std::uint16_t xMin = header.u16le();
    const std::uint16_t yMin = header.u16le();
    const std::uint16_t xMax = header.u16le();
    const std::uint16_t yMax = header.u16le();
    const std::uint16_t horizontalDpi = header.u16le();
    const std::uint16_t verticalDpi = header.u16le();
    header.skip(48);
    header.u8();
    const std::uint8_t planes = header.u8();
    const std::uint16_t bytesPerLine = header.u16le();
    header.skip(2);
    const std::uint16_t horizontalScreen = header.u16le();
    const std::uint16_t verticalScreen = header.u16le();

    const int width = xMax >= xMin ? static_cast<int>(xMax - xMin) + 1 : 0;
    const int height = yMax >= yMin ? static_cast<int>(yMax - yMin) + 1 : 0;
    const int channels = planes != 0 ? planes : 1;
    const int bitsTotal = bitsPerPixel * channels;

    record.width = width;
    record.height = height;
    record.geometryValid = width > 0 && height > 0;
    record.bitsPerPixel = bitsTotal;
    record.sampleDepth = bitsPerPixel;
    record.channels = channels;
    record.colorModel = pictureTypeText(channels);
    if (channels == 1) {
        record.colorModel += ", 2 цвета";
    }
    record.compression = encoding == 1
                             ? "RLE (побайтовое сжатие PCX)"
                             : "без сжатия (encoding = 0)";
    record.compressionCode = "PCX encoding = " + text(static_cast<long long>(encoding));

    if (horizontalDpi > 0 && verticalDpi > 0) {
        record.dpiX = horizontalDpi;
        record.dpiY = verticalDpi;
        record.dpiKnown = true;
        record.resolutionUnit = "дюйм (поля hdpi и vdpi заголовка)";
    } else {
        record.resolutionUnit = "не задано (поля hdpi и vdpi равны нулю)";
    }

    std::uint32_t paletteColors = 0;
    bool paletteGrayscale = true;
    bool paletteFound = false;
    if (version >= 5 && channels == 4 && bitsPerPixel == 8) {
        std::vector<std::uint8_t> tail;
        if (file.readTail(769, tail) && tail.size() == 769 && tail[0] == 0x0C) {
            paletteFound = true;
            paletteColors = 256;
            std::vector<std::uint32_t> seen;
            for (std::uint32_t i = 0; i < 256; ++i) {
                const std::uint32_t red = tail[1 + i * 3 + 0];
                const std::uint32_t green = tail[1 + i * 3 + 1];
                const std::uint32_t blue = tail[1 + i * 3 + 2];
                if (std::find(seen.begin(), seen.end(), (red << 16) | (green << 8) | blue) == seen.end()) {
                    seen.push_back((red << 16) | (green << 8) | blue);
                }
                if (red != green || green != blue) {
                    paletteGrayscale = false;
                }
            }
            paletteColors = static_cast<std::uint32_t>(seen.size());
        }
    }

    if (manufacturer != 0x0A) {
        addProblem(record,
                   "Неизвестный производитель 0x" + text(static_cast<long long>(manufacturer)) +
                       " вместо 0x0A (ZSoft)",
                   CheckState::Warning);
    }
    if (version > 5) {
        addProblem(record, "Номер версии " + text(static_cast<long long>(version)) + " больше 5",
                   CheckState::Warning);
    }
    if (encoding > 1) {
        addProblem(record,
                   "Неизвестный метод кодирования: " + text(static_cast<long long>(encoding)),
                   CheckState::Corrupted);
    }
    if (xMax < xMin || yMax < yMin) {
        addProblem(record,
                   "Границы изображения перепутаны: xmin=" + text(static_cast<long long>(xMin)) +
                       ", xmax=" + text(static_cast<long long>(xMax)) + ", ymin=" +
                       text(static_cast<long long>(yMin)) + ", ymax=" +
                       text(static_cast<long long>(yMax)),
                   CheckState::Corrupted);
    }
    if (planes == 0 || planes > 4) {
        addProblem(record,
                   "Недопустимое число плоскостей: " + text(static_cast<long long>(planes)),
                   CheckState::Corrupted);
    }
    if (bitsPerPixel != 1 && bitsPerPixel != 2 && bitsPerPixel != 4 && bitsPerPixel != 8) {
        addProblem(record,
                   "Недопустимое число бит на плоскость: " +
                       text(static_cast<long long>(bitsPerPixel)),
                   CheckState::Corrupted);
    }
    const std::uint64_t requiredLine =
        bytesForSamples(static_cast<std::uint64_t>(width), bitsPerPixel);
    if (bytesPerLine < requiredLine) {
        addProblem(record,
                   "BytesPerLine = " + text(static_cast<long long>(bytesPerLine)) +
                       " меньше необходимых " + text(static_cast<long long>(requiredLine)) +
                       " Б на строку — данные будут обрезаны",
                   CheckState::Corrupted);
    }
    const std::uint64_t rawSize = static_cast<std::uint64_t>(bytesPerLine) *
                                  static_cast<std::uint64_t>(height) * planes;
    if (encoding == 1 && file.size() < 128 + rawSize / 2) {
        addProblem(record,
                   "Файл весит " + humanSize(file.size()) + ", даже полностью сжатые данные "
                   "занимают не меньше " + humanSize(rawSize / 2) + " — файл сильно обрезан",
                   CheckState::Corrupted);
    }
    if (!paletteFound && version >= 5 && channels == 4) {
        addField(record, "Глубина", "Палитра 256 цветов",
                 "не найдена в конце файла",
                 "Для четвёртой версии палитра хранится последними 769 байтами и начинается с "
                 "маркера 0x0C.");
    }

    addField(record, "Формат", "Сигнатура",
             "0A (производитель ZSoft), затем номер версии и метод кодирования",
             "PCX не имеет магического числа из четырёх байт: проверяются первый байт и поля "
             "версии и кодирования.");
    addField(record, "Формат", "Версия", versionText(version),
             "Версия определяет, есть ли встроенная палитра из 256 цветов.");
    addField(record, "Формат", "Кодирование", encoding == 1 ? "1 — RLE" : "0 — без сжатия",
             "При RLE повторяющиеся байты заменяются парой «счётчик, значение».");
    addField(record, "Геометрия", "Границы",
             "x " + text(static_cast<long long>(xMin)) + "…" + text(static_cast<long long>(xMax)) +
                 ", y " + text(static_cast<long long>(yMin)) + "…" +
                 text(static_cast<long long>(yMax)),
             "В отличие от большинства форматов PCX хранит не ширину, а координаты углов.");
    addField(record, "Геометрия", "Ширина x высота", text(width) + " x " + text(height),
             "Ширина = xmax - xmin + 1, высота = ymax - ymin + 1.");
    addField(record, "Глубина", "Бит на плоскость",
             text(static_cast<long long>(bitsPerPixel)),
             "Сколько бит отведено на одну плоскость: 1, 2, 4 или 8.");
    addField(record, "Глубина", "Плоскостей (каналов)", text(static_cast<long long>(channels)),
             "Одна плоскость — чёрно-белое или серое, три — RGB, четыре — RGBA или палитра.");
    addField(record, "Глубина", "Бит на пиксель",
             text(static_cast<long long>(bitsPerPixel)) + " x " +
                 text(static_cast<long long>(channels)) + " = " + text(static_cast<long long>(bitsTotal)),
             "Глубина цвета равна произведению битов на плоскость и числа плоскостей.");
    addField(record, "Глубина", "Модель цвета", record.colorModel,
             "Определяется по числу плоскостей: 3 плоскости по 8 бит дают RGB.");
    if (paletteFound) {
        addField(record, "Глубина", "Палитра 256 цветов",
                 text(static_cast<long long>(paletteColors)) + " различных цветов" +
                     (paletteGrayscale ? ", градация серого" : ", цветная"),
                 "Последние 769 байт файла: маркер 0x0C и 256 троек RGB.");
    } else if (channels <= 2) {
        addField(record, "Глубина", "Встроенная палитра EGA",
                 "16 цветов, первые 48 байт после полей разрешения",
                 "Используется версиями 2 и 3; в файле всегда присутствует, даже если не нужна.");
    }
    addField(record, "Разрешение", "hdpi / vdpi",
             text(static_cast<long long>(horizontalDpi)) + " / " +
                 text(static_cast<long long>(verticalDpi)),
             "PCX хранит разрешение напрямую в dpi, без пересчёта из метров.");
    addField(record, "Разрешение", "DPI",
             record.dpiKnown ? number(record.dpiX) + " x " + number(record.dpiY) + " dot/inch"
                             : "не задано (нули в заголовке)",
             "Значения берутся как есть; часто они условные и не соответствуют реальному "
             "физическому размеру.");
    addField(record, "Сжатие", "Метод", record.compression,
             "RLE: повторяющийся байт записывается как 0xC0 | count, затем значение.");
    addField(record, "Дополнительно", "Размер экрана",
             text(static_cast<long long>(horizontalScreen)) + " x " +
                 text(static_cast<long long>(verticalScreen)),
             "Поля hscreen и vscreen: рекомендуемый размер окна для отображения, на изображение "
             "не влияет.");
    addField(record, "Сжатие", "Байт на строку", text(static_cast<long long>(bytesPerLine)),
             "Строки выровнены по байтам; это поле важнее ширины при декодировании.");
    addField(record, "Сжатие", "Несжатый размер", humanSize(rawSize),
             "bytesPerLine x height — размер, который занимали бы пиксели без сжатия.");
    if (file.size() > 128) {
        addField(record, "Сжатие", "Коэффициент сжатия",
                 number(static_cast<double>(rawSize) /
                        static_cast<double>(file.size() - 128)) +
                     " : 1",
                 "Отношение несжатого размера данных к размеру файла за вычетом заголовка.");
    }
    addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
             "Проверяются границы изображения, число плоскостей и минимально возможный размер "
             "файла.");

    return true;
}

}