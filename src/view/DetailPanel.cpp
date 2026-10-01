#include "view/DetailPanel.h"

#include <FL/fl_draw.H>

#include <algorithm>
#include <cstddef>

#include "model/ImageFormat.h"

namespace lab2 {

namespace {

const char* const kHeaders[] = {"Раздел", "Поле", "Значение", "Пояснение"};
constexpr int kColumnCount = 4;
constexpr int kCellPadding = 6;
constexpr int kFontSize = 13;
constexpr int kLineGap = 4;
constexpr int kMinRowHeight = 22;
const int kColumnFractions[kColumnCount] = {13, 17, 22, 48};

std::vector<std::string> wrapCell(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    if (maxWidth <= 0) {
        return lines;
    }
    fl_font(FL_HELVETICA, kFontSize);
    std::string current;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) {
            return;
        }
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && static_cast<int>(fl_width(candidate.c_str())) > maxWidth) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
        word.clear();
    };
    for (char symbol : text) {
        if (symbol == ' ' || symbol == '\n') {
            flushWord();
        } else {
            word += symbol;
        }
    }
    flushWord();
    if (!current.empty()) {
        lines.push_back(current);
    }
    if (lines.empty()) {
        lines.emplace_back();
    }
    std::vector<std::string> result;
    for (std::string& line : lines) {
        std::string rest = line;
        while (static_cast<int>(fl_width(rest.c_str())) > maxWidth && rest.size() > 1) {
            std::size_t cut = rest.size() - 1;
            while (cut > 1 && static_cast<int>(fl_width(rest.substr(0, cut).c_str())) > maxWidth) {
                --cut;
            }
            result.push_back(rest.substr(0, cut));
            rest = rest.substr(cut);
        }
        result.push_back(rest);
    }
    return result;
}

std::string formatBytes(std::uint64_t bytes) {
    static const char* const units[] = {"Б", "КБ", "МБ", "ГБ"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.2f %s", value, units[unit]);
    return buffer;
}

std::string formatMean(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

void addRow(std::vector<DetailPanel::Row>& rows, std::string group, std::string name,
            std::string value, std::string note) {
    rows.push_back({std::move(group), std::move(name), std::move(value), std::move(note)});
}

}

DetailPanel::DetailPanel(int x, int y, int w, int h, const char* label)
    : Fl_Table_Row(x, y, w, h, label) {
    cols(kColumnCount);
    col_header_height(24);
    col_header(1);
    row_header(0);
    row_height_all(kMinRowHeight);
    end();
    layoutReady_ = false;
    applyColumnWidths();
    showMessage("Файл не выбран");
}

void DetailPanel::applyColumnWidths() {
    fl_font(FL_HELVETICA_BOLD, kFontSize);
    const int available = std::max(240, w() - 2);
    int widths[kColumnCount] = {};
    int used = 0;
    for (int i = 0; i < kColumnCount; ++i) {
        const int headerWidth =
            static_cast<int>(fl_width(kHeaders[i])) + 2 * kCellPadding;
        int width = std::max(available * kColumnFractions[i] / 100, headerWidth);
        if (i == kColumnCount - 1) {
            width = std::max(width, available - used);
        } else {
            used += width;
        }
        widths[i] = width;
    }
    for (int i = 0; i < kColumnCount; ++i) {
        col_width(i, widths[i]);
    }
}

void DetailPanel::updateRowHeights() {
    fl_font(FL_HELVETICA, kFontSize);
    const int lineHeight = fl_height() + kLineGap;
    wrapped_.assign(rows_.size(), {});
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& item = rows_[i];
        const std::string* texts[kColumnCount] = {&item.group, &item.name, &item.value,
                                                  &item.note};
        int lines = 1;
        for (int column = 0; column < kColumnCount; ++column) {
            std::vector<std::string> cell =
                wrapCell(*texts[column], col_width(column) - 2 * kCellPadding);
            lines = std::max(lines, static_cast<int>(cell.size()));
            wrapped_[i].push_back(std::move(cell));
        }
        row_height(static_cast<int>(i) + 1, std::max(kMinRowHeight, lines * lineHeight + 6));
    }
    if (rows_.empty()) {
        messageLines_ = wrapCell(message_, std::max(40, w() - 4 * kCellPadding));
        row_height(0, std::max(kMinRowHeight,
                               static_cast<int>(messageLines_.size()) * lineHeight + 6));
    }
}

void DetailPanel::refreshLayout() {
    applyColumnWidths();
    updateRowHeights();
    redraw();
}

void DetailPanel::resize(int x, int y, int width, int height) {
    Fl_Table_Row::resize(x, y, width, height);
    if (layoutReady_) {
        refreshLayout();
    }
}

void DetailPanel::showMessage(const std::string& text) {
    rows_.clear();
    message_ = text;
    rows(1);
    layoutReady_ = true;
    refreshLayout();
}

void DetailPanel::showRecord(const ImageRecord* record) {
    rows_.clear();
    if (record == nullptr) {
        showMessage("Файл не выбран");
        return;
    }
    message_.clear();

    addRow(rows_, "Файл", "Имя", record->name,
           "Имя файла на диске. Используется как заголовок строки в таблице.");
    addRow(rows_, "Файл", "Папка", record->folder,
           "Расположение файла, из которого был запущен анализ.");
    addRow(rows_, "Файл", "Размер", formatBytes(record->fileSize),
           "Полный размер файла в байтах.");
    addRow(rows_, "Файл", "Прочитано байт", std::to_string(record->bytesRead),
           "Сколько байт реально прочитано: заголовки читаются лениво, поэтому значение обычно "
           "намного меньше размера файла.");

    std::string signature;
    char buffer[8];
    for (int i = 0; i < 8; ++i) {
        std::snprintf(buffer, sizeof(buffer), "%02X", record->signature[i]);
        signature += buffer;
        signature += ' ';
    }
    addRow(rows_, "Формат", "Сигнатура", signature,
           "Первые 8 байт файла. По ним определяется формат независимо от расширения.");
    addRow(rows_, "Формат", "Определён как", formatFullName(record->format),
           "Название формата, соответствующее сигнатуре.");
    addRow(rows_, "Формат", "Расширение", record->extensionName,
           record->extensionMismatch
               ? "Расширение не совпадает с определённым форматом, это признак переименованного файла."
               : "Расширение файла. Используется только для сравнения с сигнатурой.");

    if (record->width > 0 && record->height > 0) {
        addRow(rows_, "Размер", "Ширина", std::to_string(record->width) + " px",
               "Ширина изображения в пикселях из поля заголовка.");
        addRow(rows_, "Размер", "Высота", std::to_string(record->height) + " px",
               "Высота изображения в пикселях из поля заголовка.");
    }
    if (record->dpiKnown) {
        char dpi[64];
        std::snprintf(dpi, sizeof(dpi), "%.2f x %.2f", record->dpiX, record->dpiY);
        addRow(rows_, "Размер", "Разрешение", dpi,
               "Разрешение пересчитано в точки на дюйм. Источник значения зависит от формата.");
        addRow(rows_, "Размер", "Единицы", record->resolutionUnit,
               "Единицы, в которых записано разрешение в файле.");
    } else {
        addRow(rows_, "Размер", "Разрешение", "не задано",
               "Поле разрешения в файле равно нулю, поэтому DPI не определены.");
    }

    if (record->bitsPerPixel > 0) {
        addRow(rows_, "Глубина", "Бит на пиксель", std::to_string(record->bitsPerPixel),
               "Суммарное число бит на пиксель, то есть глубина цвета всей точки.");
    }
    if (record->sampleDepth > 0) {
        addRow(rows_, "Глубина", "Бит на канал", std::to_string(record->sampleDepth),
               "Число бит в одном канале цвета.");
    }
    if (record->channels > 0) {
        addRow(rows_, "Глубина", "Каналы", std::to_string(record->channels),
               "Число цветовых каналов: один - оттенки серого, три - RGB.");
    }
    if (!record->colorModel.empty()) {
        addRow(rows_, "Глубина", "Модель цвета", record->colorModel,
               "Способ интерпретации значений пикселя, указанный в файле.");
    }

    if (!record->compression.empty()) {
        addRow(rows_, "Сжатие", "Метод", record->compression,
               "Алгоритм, которым сжаты пиксели внутри файла.");
    }
    if (!record->compressionCode.empty()) {
        addRow(rows_, "Сжатие", "Код", record->compressionCode,
               "Числовой код метода сжатия из заголовка файла.");
    }

    addRow(rows_, "Проверка", "Статус", checkStateName(record->check),
           record->check == CheckState::Ok
               ? "Сигнатура и обязательные поля заголовка согласованы."
               : "См. список замечаний ниже.");
    for (const std::string& problem : record->problems) {
        addRow(rows_, "Проверка", "Замечание", problem,
               "Описание найденного несоответствия или повреждения.");
    }

    if (record->pixelsAnalyzed) {
        addRow(rows_, "Пиксели", "Декодер", record->pixels.decoder,
               "Собственный декодер, который восстановил пиксели изображения.");
        addRow(rows_, "Пиксели", "Всего цветов", std::to_string(record->pixels.distinctColors),
               "Число различных цветов среди всех пикселей.");
        addRow(rows_, "Пиксели", "Оттенки серого", record->pixels.grayscale ? "да" : "нет",
               "Признак того, что во всех пикселях каналы R, G и B равны.");
        addRow(rows_, "Пиксели", "Прозрачность", record->pixels.alphaUsed ? "используется" : "нет",
               "Есть ли в изображении непрозрачные пиксели.");
        addRow(rows_, "Пиксели", "Средний R", formatMean(record->pixels.meanChannel[0]),
               "Среднее значение красного канала по всем пикселям.");
        addRow(rows_, "Пиксели", "Средний G", formatMean(record->pixels.meanChannel[1]),
               "Среднее значение зелёного канала по всем пикселям.");
        addRow(rows_, "Пиксели", "Средний B", formatMean(record->pixels.meanChannel[2]),
               "Среднее значение синего канала по всем пикселям.");
        addRow(rows_, "Пиксели", "Энтропия", formatMean(record->pixels.entropy),
               "Энтропия гистограммы яркости: чем выше значение, тем менее однородно изображение.");
        if (record->pixels.compressionRatio > 0.0) {
            addRow(rows_, "Пиксели", "Коэффициент сжатия",
                   formatMean(record->pixels.compressionRatio) + " : 1",
                   "Во сколько раз размер файла меньше размера распакованных пикселей.");
        }
        if (!record->pixels.error.empty()) {
            addRow(rows_, "Пиксели", "Ошибка декодирования", record->pixels.error,
                   "Изображение показано частично, если поток данных оборвался.");
        }
    }

    for (const Field& field : record->details) {
        addRow(rows_, field.group, field.name, field.value, field.note);
    }

    rows(static_cast<int>(rows_.size()) + 1);
    row_position(0);
    col_position(0);
    layoutReady_ = true;
    refreshLayout();
}

void DetailPanel::draw_cell(TableContext context, int row, int column, int cellX, int cellY,
                            int cellW, int cellH) {
    switch (context) {
        case CONTEXT_COL_HEADER: {
            fl_push_clip(cellX, cellY, cellW, cellH);
            fl_draw_box(FL_THIN_UP_BOX, cellX, cellY, cellW, cellH, FL_BACKGROUND_COLOR);
            fl_color(FL_BLACK);
            fl_font(FL_HELVETICA_BOLD, kFontSize);
            fl_draw(kHeaders[column], cellX + kCellPadding, cellY + cellH / 2 + 5);
            fl_pop_clip();
            return;
        }
        case CONTEXT_CELL: {
            fl_push_clip(cellX, cellY, cellW, cellH);
            fl_color(row % 2 == 0 ? FL_WHITE : fl_rgb_color(248, 248, 250));
            fl_rectf(cellX, cellY, cellW, cellH);
            fl_color(FL_LIGHT2);
            fl_rect(cellX, cellY, cellW, cellH);
            fl_font(FL_HELVETICA, kFontSize);
            const int lineHeight = fl_height() + kLineGap;
            if (rows_.empty()) {
                fl_color(fl_rgb_color(90, 90, 90));
                int lineY = cellY + kCellPadding + fl_height();
                for (const std::string& line : messageLines_) {
                    if (lineY > cellY + cellH) {
                        break;
                    }
                    fl_draw(line.c_str(), cellX + kCellPadding, lineY);
                    lineY += lineHeight;
                }
                fl_pop_clip();
                return;
            }
            const std::size_t index = static_cast<std::size_t>(row - 1);
            if (row < 1 || index >= rows_.size() || index >= wrapped_.size() ||
                static_cast<std::size_t>(column) >= wrapped_[index].size()) {
                fl_pop_clip();
                return;
            }
            fl_color(column == 2 ? fl_rgb_color(0, 60, 120) : FL_BLACK);
            int lineY = cellY + kCellPadding + fl_height();
            for (const std::string& line : wrapped_[index][static_cast<std::size_t>(column)]) {
                if (lineY > cellY + cellH) {
                    break;
                }
                fl_draw(line.c_str(), cellX + kCellPadding, lineY);
                lineY += lineHeight;
            }
            fl_pop_clip();
            return;
        }
        default:
            return;
    }
}

}