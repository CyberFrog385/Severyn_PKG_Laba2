#include "core/parsers/Parsers.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/ByteReader.h"
#include "core/ScanWindow.h"
#include "core/parsers/ParserCommon.h"

namespace lab2 {

namespace {

constexpr std::size_t kBlockBytes = 32 * 1024;
constexpr std::size_t kMaxBytes = 8 * 1024 * 1024;
constexpr int kMaxFrames = 20000;

std::string disposalText(int disposal) {
    switch (disposal) {
        case 0:
            return "0 — не задано";
        case 1:
            return "1 — не менять";
        case 2:
            return "2 — очистить в фон";
        case 3:
            return "3 — восстановить до предыдущего";
        default:
            return text(disposal);
    }
}

std::string interlaceText(bool interlaced) {
    return interlaced
               ? "да (4 прохода: строки 0/8, 4/8, 2/4, 1/2)"
               : "нет (строки идут подряд)";
}

}

bool parseGif(FileHead& file, ImageRecord& record) {
    ScanWindow window(file, kBlockBytes, kMaxBytes);
    std::vector<std::uint8_t> start;
    if (!window.readBytes(13, start) || start.size() < 13) {
        addProblem(record, "Файл короче 13 байт, нет заголовка GIF", CheckState::Corrupted);
        return false;
    }
    const bool version87 = std::memcmp(start.data(), "GIF87a", 6) == 0;
    const bool version89 = std::memcmp(start.data(), "GIF89a", 6) == 0;
    if (!version87 && !version89) {
        addProblem(record, "Заголовок GIF87a/GIF89a не найден", CheckState::Corrupted);
        return false;
    }

    ByteReader descriptor(start.data() + 6, 7);
    const std::uint16_t canvasWidth = descriptor.u16le();
    const std::uint16_t canvasHeight = descriptor.u16le();
    const std::uint8_t packed = descriptor.u8();
    const std::uint8_t backgroundIndex = descriptor.u8();
    const std::uint8_t aspectRatio = descriptor.u8();

    const bool globalPalette = (packed & 0x80) != 0;
    const int colorResolution = ((packed >> 4) & 0x07) + 1;
    const bool paletteSorted = (packed & 0x08) != 0;
    const std::uint32_t globalPaletteSize = globalPalette ? (1u << ((packed & 0x07) + 1)) : 0;

    if (globalPaletteSize != 0) {
        if (!window.ensure(static_cast<std::size_t>(globalPaletteSize) * 3)) {
            addProblem(record, "Глобальная палитра обрезана — файл меньше заявленного",
                       CheckState::Corrupted);
            return false;
        }
        window.skip(static_cast<std::size_t>(globalPaletteSize) * 3);
    }

    auto skipSubBlocks = [&window]() {
        bool ok = true;
        while (window.ensure(1)) {
            const std::uint8_t size = window.peek();
            window.skip(1);
            if (size == 0) {
                break;
            }
            if (!window.ensure(size)) {
                ok = false;
                break;
            }
            window.skip(size);
        }
        return ok;
    };

    int frames = 0;
    int frameWidth = 0;
    int frameHeight = 0;
    int frameLeft = 0;
    int frameTop = 0;
    bool firstFrameInterlaced = false;
    bool anyInterlaced = false;
    int localPalettes = 0;
    std::uint32_t largestLocalPalette = 0;
    bool transparency = false;
    std::uint8_t transparentIndex = 0;
    int minimumDelay = 0;
    int maximumDelay = 0;
    int totalDelay = 0;
    int loopCount = -1;
    std::string comment;
    std::string application;
    std::uint64_t lzwBytes = 0;
    bool trailerFound = false;
    bool structureBroken = false;
    bool badLzwCodeSize = false;
    int lzwMinCodeSize = 0;

    while (!window.atEnd()) {
        if (!window.ensure(1)) {
            break;
        }
        const std::uint8_t block = window.take();
        if (block == 0x3B) {
            trailerFound = true;
            break;
        }
        if (block == 0x21) {
            if (!window.ensure(1)) {
                structureBroken = true;
                break;
            }
            const std::uint8_t label = window.take();
            if (label == 0xF9) {
                if (!window.ensure(6)) {
                    structureBroken = true;
                    break;
                }
                const std::uint8_t size = window.take();
                if (size != 4 || !window.ensure(4)) {
                    structureBroken = true;
                    break;
                }
                const std::uint8_t gcePacked = window.take();
                const std::uint8_t delayLow = window.take();
                const std::uint8_t delayHigh = window.take();
                const std::uint8_t transparent = window.take();
                if (window.ensure(1) && window.peek() == 0) {
                    window.skip(1);
                }
                if ((gcePacked & 0x01) != 0) {
                    transparency = true;
                    transparentIndex = transparent;
                }
                const int delay = delayLow | (delayHigh << 8);
                const int disposal = (gcePacked >> 2) & 0x07;
                if (disposal > 0) {
                    addField(record, "Дополнительно", "Метод удаления кадра",
                             disposalText(disposal),
                             "Расширение Graphic Control (0xF9): что делать с предыдущим кадром.");
                }
                if (frames == 0) {
                    minimumDelay = delay;
                    maximumDelay = delay;
                }
                totalDelay += delay;
                minimumDelay = std::min(minimumDelay, delay);
                maximumDelay = std::max(maximumDelay, delay);
            } else if (label == 0xFE) {
                std::string text;
                while (window.ensure(1)) {
                    const std::uint8_t size = window.take();
                    if (size == 0) {
                        break;
                    }
                    std::vector<std::uint8_t> part;
                    if (!window.readBytes(size, part)) {
                        break;
                    }
                    if (text.size() < 120) {
                        text.append(reinterpret_cast<const char*>(part.data()),
                                    std::min<std::size_t>(part.size(), 120 - text.size()));
                    }
                }
                if (comment.empty()) {
                    comment = text;
                }
            } else if (label == 0xFF) {
                std::vector<std::uint8_t> identifier;
                if (window.readBytes(1, identifier) && identifier[0] == 11 &&
                    window.readBytes(11, identifier) && application.empty()) {
                    for (std::size_t i = 0; i < 8; ++i) {
                        const char c = static_cast<char>(identifier[i]);
                        if (c == 0 || c < 32) {
                            break;
                        }
                        application.push_back(c);
                    }
                }
                bool firstSubBlock = application.find("NETSCAPE") != std::string::npos;
                while (window.ensure(1)) {
                    const std::uint8_t size = window.take();
                    if (size == 0) {
                        break;
                    }
                    if (!window.ensure(size)) {
                        structureBroken = true;
                        break;
                    }
                    if (firstSubBlock && size >= 3 && window.peek() == 1) {
                        const std::uint8_t loopLow = window.cursor()[1];
                        const std::uint8_t loopHigh = window.cursor()[2];
                        loopCount = loopLow | (loopHigh << 8);
                    }
                    firstSubBlock = false;
                    window.skip(size);
                }
            } else {
                if (!skipSubBlocks()) {
                    structureBroken = true;
                }
            }
            continue;
        }
        if (block == 0x2C) {
            if (frames >= kMaxFrames) {
                break;
            }
            if (!window.ensure(9)) {
                structureBroken = true;
                break;
            }
            frameLeft = window.take() | (window.take() << 8);
            frameTop = window.take() | (window.take() << 8);
            frameWidth = window.take() | (window.take() << 8);
            frameHeight = window.take() | (window.take() << 8);
            const std::uint8_t imagePacked = window.take();
            const bool interlaced = (imagePacked & 0x40) != 0;
            const bool localPalette = (imagePacked & 0x80) != 0;
            const std::uint32_t localSize = localPalette ? (1u << ((imagePacked & 0x07) + 1)) : 0;
            if (localPalette) {
                ++localPalettes;
                largestLocalPalette = std::max(largestLocalPalette, localSize);
            }
            if (frames == 0) {
                firstFrameInterlaced = interlaced;
            }
            anyInterlaced = anyInterlaced || interlaced;
            if (localPalette) {
                if (!window.ensure(localSize * 3)) {
                    structureBroken = true;
                    break;
                }
                window.skip(static_cast<std::size_t>(localSize) * 3);
            }
            if (!window.ensure(1)) {
                structureBroken = true;
                break;
            }
            const std::uint8_t codeSize = window.take();
            if (codeSize < 2 || codeSize > 11) {
                badLzwCodeSize = true;
                structureBroken = true;
                break;
            }
            if (frames == 0) {
                lzwMinCodeSize = codeSize;
            }
            while (window.ensure(1)) {
                const std::uint8_t size = window.take();
                if (size == 0) {
                    break;
                }
                lzwBytes += size;
                if (!window.ensure(size)) {
                    structureBroken = true;
                    break;
                }
                window.skip(size);
            }
            ++frames;
            continue;
        }
        structureBroken = true;
        break;
    }

    record.width = frames > 0 ? frameWidth : canvasWidth;
    record.height = frames > 0 ? frameHeight : canvasHeight;
    record.geometryValid = record.width > 0 && record.height > 0;
    record.bitsPerPixel = lzwMinCodeSize > 0 ? lzwMinCodeSize : 8;
    record.sampleDepth = record.bitsPerPixel;
    record.channels = 1;
    record.colorModel = globalPaletteSize != 0
                            ? "палитра " + text(static_cast<long long>(globalPaletteSize)) + " цветов"
                            : "палитра кадра";
    record.compression = frames > 1 ? "LZW (анимированный GIF)" : "LZW";
    record.compressionCode = "GIF LZW";
    record.resolutionUnit = "GIF не хранит разрешение (вместо него — соотношение сторон пикселя)";

    if (!record.geometryValid) {
        addProblem(record,
                   "Нулевые размеры: холст " + text(static_cast<long long>(canvasWidth)) + " x " +
                       text(static_cast<long long>(canvasHeight)),
                   CheckState::Corrupted);
    }
    if (!trailerFound) {
        addProblem(record, "В конце файла нет завершающего байта 0x3B — файл оборван",
                   CheckState::Corrupted);
    }
    if (frames == 0) {
        addProblem(record, "Не найден ни один кадр (маркер 0x2C)", CheckState::Corrupted);
    }
    if (badLzwCodeSize) {
        addProblem(record, "Недопустимый размер кода LZW — структура кадра повреждена",
                   CheckState::Corrupted);
    }
    if (structureBroken && trailerFound) {
        addProblem(record, "Обход блоков прерван из-за нарушения структуры", CheckState::Warning);
    }
    if (frameWidth > canvasWidth || frameHeight > canvasHeight) {
        addProblem(record,
                   "Кадр " + text(frameWidth) + " x " + text(frameHeight) + " больше холста " +
                       text(static_cast<long long>(canvasWidth)) + " x " +
                       text(static_cast<long long>(canvasHeight)),
                   CheckState::Warning);
    }

    const std::uint64_t rawBytes = bytesForSamples(
        static_cast<std::uint64_t>(std::max(frameWidth, static_cast<int>(canvasWidth))) *
            std::max(frameHeight, static_cast<int>(canvasHeight)),
        record.bitsPerPixel);

    addField(record, "Формат", "Сигнатура",
             version89 ? "GIF89a" : "GIF87a",
             "Шесть байт в начале; различие версий — доступность прозрачности и комментариев.");
    addField(record, "Формат", "Экран (Logical Screen Descriptor)",
             text(static_cast<long long>(canvasWidth)) + " x " +
                 text(static_cast<long long>(canvasHeight)),
             "Размеры холста; для анимации это размер первого кадра.");
    addField(record, "Геометрия", "Ширина x высота",
             text(record.width) + " x " + text(record.height),
             "Берётся из дескриптора первого кадра (маркер 0x2C), а не из холста.");
    if (frames > 1) {
        addField(record, "Геометрия", "Положение первого кадра",
                 text(frameLeft) + ", " + text(frameTop),
                 "Смещение кадра на холсте; ненулевое значение означает частичное перекрытие.");
    }
    addField(record, "Глубина", "Бит на пиксель",
             text(static_cast<long long>(record.bitsPerPixel)) +
                 " (минимальный размер кода LZW)",
             "GIF хранит не глубину, а начальный размер кода LZW: он определяет размер палитры.");
    addField(record, "Глубина", "Цветовое разрешение",
             text(colorResolution) + " бит на первичный цвет" +
                 (paletteSorted ? ", палитра отсортирована" : ""),
             "Биты 4-6 байта упаковки; при 8 бит на канал это 8 бит на первичный цвет.");
    addField(record, "Глубина", "Глобальная палитра",
             globalPalette ? text(static_cast<long long>(globalPaletteSize)) + " записей по 3 Б"
                           : "отсутствует",
             "Таблица цветов изображения, индексы пикселей ссылаются на неё по номеру.");
    addField(record, "Глубина", "Локальные палитры",
             std::to_string(localPalettes) + " шт." +
                 (localPalettes > 0 ? ", наибольшая " + text(static_cast<long long>(largestLocalPalette)) : ""),
             "У отдельных кадров анимации может быть своя палитра, она идёт сразу после "
             "дескриптора кадра.");
    if (transparency) {
        addField(record, "Глубина", "Прозрачный индекс",
                 text(static_cast<long long>(transparentIndex)),
                 "Расширение Graphic Control (0xF9); пиксели с таким индексом не рисуются.");
    }
    if (globalPalette) {
        addField(record, "Глубина", "Индекс фонового цвета",
                 text(static_cast<long long>(backgroundIndex)),
                 "Байт экрана: индекс цвета, которым заполняются прозрачные области.");
    }
    addField(record, "Разрешение", "Разрешение", "не хранится в формате",
             "В GIF нет полей dpi: байт соотношения сторон (в данном файле " +
                 text(static_cast<long long>(aspectRatio)) +
                 ") описывает форму пикселя, а не физический размер.");
    addField(record, "Сжатие", "Метод", "LZW, размер кода " + text(static_cast<long long>(lzwMinCodeSize)),
             "Алгоритм Лемпеля — Зива — Уэлча: словарь пополняется по ходу декодирования.");
    addField(record, "Сжатие", "Сжатых данных (подблоки)", humanSize(lzwBytes),
             "Подблоки пропускаются по длинам, сами сжатые данные не читаются.");
    addField(record, "Сжатие", "Коэффициент сжатия",
             lzwBytes > 0 ? number(static_cast<double>(rawBytes) / static_cast<double>(lzwBytes)) + " : 1"
                           : "не рассчитан",
             "Отношение несжатого размера пикселей к суммарному объёму подблоков LZW.");
    addField(record, "Дополнительно", "Кадров", text(frames),
             frames > 1 ? "Каждый кадр начинается с маркера 0x2C — это анимация."
                        : "Один кадр — статичное изображение.");
    if (frames > 1) {
        addField(record, "Дополнительно", "Суммарная задержка", text(totalDelay) + " x 1/100 с",
                 "Сумма задержек всех кадров из расширений Graphic Control; определяет длительность "
                 "анимации.");
        addField(record, "Дополнительно", "Задержка кадра",
                 text(minimumDelay) + "–" + text(maximumDelay) + " x 1/100 с",
                 "Минимальная и максимальная задержка отдельных кадров.");
        addField(record, "Дополнительно", "Чередование строк", interlaceText(firstFrameInterlaced),
                 "Дескриптор кадра, бит 6: изображение записано четырьмя проходами.");
        if (loopCount >= 0) {
            addField(record, "Дополнительно", "Число повторов",
                     loopCount == 0 ? "бесконечно" : text(loopCount),
                     "Расширение Application " + application + " (Netscape 2.0), подблок с числом циклов.");
        }
        if (anyInterlaced && !firstFrameInterlaced) {
            addField(record, "Дополнительно", "Чередование строк", "встречается в отдельных кадрах",
                     "Часть кадров анимации записана с чередованием строк.");
        }
    }
    if (!application.empty()) {
        addField(record, "Дополнительно", "Расширение Application", application,
                 "Идентификатор расширения: XMP, Netscape2.0, Adobe и другие.");
    }
    if (!comment.empty()) {
        addField(record, "Дополнительно", "Комментарий", comment,
                 "Расширение Comment (0xFE): произвольный текст.");
    }
    addField(record, "Проверка", "Завершающий байт 0x3B",
             trailerFound ? "найден" : "НЕ НАЙДЕН",
             "Терминатор обязателен; его отсутствие означает обрыв файла.");
    addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
             "Проверяются размеры кадра, допустимость размера кода LZW и наличие терминатора.");

    return true;
}

}