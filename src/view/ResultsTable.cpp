#include "view/ResultsTable.h"

#include <FL/fl_draw.H>

#include <algorithm>

#include "model/ImageFormat.h"
#include "view/FieldHelp.h"
#include "view/PathUtil.h"

namespace lab2 {

namespace {

const char* const kHeaders[] = {"Файл", "Формат", "Размер", "Разрешение", "Бит/пикс",
                                 "Сжатие", "Статус", "Цветов"};
constexpr int kColumnCount = 8;
constexpr int kCellPadding = 6;
const int kDefaultWidths[kColumnCount] = {230, 60, 100, 120, 70, 140, 110, 70};

Fl_Color statusColor(CheckState state) {
    switch (state) {
        case CheckState::Ok:
            return fl_rgb_color(20, 120, 40);
        case CheckState::Warning:
            return fl_rgb_color(180, 120, 0);
        case CheckState::Corrupted:
            return fl_rgb_color(190, 30, 30);
        case CheckState::NotImage:
            return fl_rgb_color(120, 60, 160);
    }
    return FL_BLACK;
}

std::string formatDpiValue(const ImageRecord& record) {
    if (!record.dpiKnown) {
        return "не задано";
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.0f x %.0f %s", record.dpiX, record.dpiY,
                  record.resolutionUnit == "дюйм" ? "dpi" : record.resolutionUnit.c_str());
    return buffer;
}

}

ResultsTable::ResultsTable(int x, int y, int w, int h, const char* label)
    : Fl_Table_Row(x, y, w, h, label) {
    cols(kColumnCount);
    col_header_height(24);
    col_header(1);
    row_header(0);
    row_height_all(22);
    col_resize(1);
    end();
    applyColumnWidths();
}

void ResultsTable::applyColumnWidths() {
    fl_font(FL_HELVETICA_BOLD, 13);
    int base[kColumnCount] = {};
    int sum = 0;
    for (int i = 0; i < kColumnCount; ++i) {
        const int needed = static_cast<int>(fl_width(kHeaders[i])) + 2 * kCellPadding;
        base[i] = std::max(kDefaultWidths[i], needed);
        sum += base[i];
    }
    const int available = std::max(120, w() - 2);
    int widths[kColumnCount] = {};
    if (sum <= available) {
        for (int i = 0; i < kColumnCount; ++i) {
            widths[i] = base[i];
        }
        widths[0] += available - sum;
    } else {
        int excess = sum - available;
        int pool = 0;
        for (int i = 1; i < kColumnCount; ++i) {
            widths[i] = base[i];
            pool += base[i];
        }
        widths[0] = base[0];
        int removed = 0;
        for (int i = 1; i < kColumnCount; ++i) {
            const int give = static_cast<int>(
                static_cast<long long>(excess) * widths[i] / std::max(1, pool));
            const int actual = std::min(give, widths[i] - 24);
            widths[i] -= actual;
            removed += actual;
        }
        widths[0] -= excess - removed;
    }
    for (int i = 0; i < kColumnCount; ++i) {
        col_width(i, std::max(24, widths[i]));
    }
    redraw();
}

void ResultsTable::resize(int x, int y, int width, int height) {
    Fl_Table_Row::resize(x, y, width, height);
    applyColumnWidths();
}

int ResultsTable::headerHeight() const { return 24; }

void ResultsTable::rebuildVisible() {
    visible_.clear();
    if (records_ == nullptr) {
        rows(0);
        return;
    }
    if (filter_.empty()) {
        visible_.reserve(records_->size());
        for (std::size_t i = 0; i < records_->size(); ++i) {
            visible_.push_back(i);
        }
    } else {
        std::string needle = filter_;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (std::size_t i = 0; i < records_->size(); ++i) {
            const ImageRecord& record = (*records_)[i];
            std::string haystack = record.name + " " + record.folder + " " +
                                   formatFullName(record.format) + " " + record.compression;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (haystack.find(needle) != std::string::npos) {
                visible_.push_back(i);
            }
        }
    }
    rows(static_cast<int>(visible_.size()) + 1);
    redraw();
}

void ResultsTable::applyFilter(const std::string& text) {
    filter_ = text;
    selected_ = -1;
    rebuildVisible();
    row_position(0);
    col_position(0);
}

void ResultsTable::setSelection(int row) {
    selected_ = row;
    redraw();
}

const ImageRecord* ResultsTable::recordAt(std::size_t visibleIndex) const {
    if (records_ == nullptr || visibleIndex >= visible_.size()) {
        return nullptr;
    }
    return &(*records_)[visible_[visibleIndex]];
}

void ResultsTable::draw() {
    if (records_ != nullptr && static_cast<std::size_t>(rows() - 1) != visible_.size()) {
        rebuildVisible();
    }
    Fl_Table_Row::draw();
}

void ResultsTable::draw_cell(TableContext context, int row, int column, int cellX, int cellY,
                             int cellW, int cellH) {
    switch (context) {
        case CONTEXT_COL_HEADER: {
            fl_push_clip(cellX, cellY, cellW, cellH);
            fl_draw_box(FL_THIN_UP_BOX, cellX, cellY, cellW, cellH, FL_BACKGROUND_COLOR);
            fl_color(FL_BLACK);
            fl_font(FL_HELVETICA_BOLD, 13);
            fl_draw(kHeaders[column], cellX + kCellPadding, cellY + cellH / 2 + 5);
            fl_pop_clip();
            return;
        }
        case CONTEXT_CELL: {
            const int dataRow = row - 1;
            const ImageRecord* record = recordAt(static_cast<std::size_t>(dataRow < 0 ? 0 : dataRow));
            fl_push_clip(cellX, cellY, cellW, cellH);
            fl_color(row - 1 == selected_ ? fl_rgb_color(215, 228, 245) : FL_WHITE);
            fl_rectf(cellX, cellY, cellW, cellH);
            fl_color(FL_LIGHT2);
            fl_rect(cellX, cellY, cellW, cellH);
            if (record == nullptr) {
                fl_pop_clip();
                return;
            }
            fl_font(FL_HELVETICA, 13);
            std::string text;
            Fl_Color color = FL_BLACK;
            switch (column) {
                case 0:
                    text = record->name;
                    break;
                case 1:
                    text = formatShortName(record->format);
                    break;
                case 2:
                    text = record->geometryValid ? std::to_string(record->width) + " x " +
                                                       std::to_string(record->height)
                                                 : "н/д";
                    break;
                case 3:
                    text = formatDpiValue(*record);
                    break;
                case 4:
                    text = std::to_string(record->bitsPerPixel);
                    break;
                case 5:
                    text = record->compression;
                    break;
                case 6:
                    text = checkStateName(record->check);
                    color = statusColor(record->check);
                    break;
                case 7:
                    text = record->pixelsAnalyzed ? std::to_string(record->pixels.distinctColors)
                                                  : std::string();
                    break;
                default:
                    break;
            }
            fl_color(color);
            fl_draw(text.c_str(), cellX + kCellPadding, cellY + cellH / 2 + 5);
            fl_pop_clip();
            return;
        }
        default:
            return;
    }
}

int ResultsTable::handle(int event) {
    switch (event) {
        case FL_PUSH: {
            int row = 0;
            int column = 0;
            ResizeFlag resizeFlag = RESIZE_NONE;
            if (!cursor2rowcol(row, column, resizeFlag)) {
                break;
            }
            if (resizeFlag == RESIZE_COL_LEFT || resizeFlag == RESIZE_COL_RIGHT) {
                resizeColumn_ = column;
                lastX_ = Fl::event_x();
                return 1;
            }
            selected_ = std::max(-1, row - 1);
            redraw();
            do_callback(CONTEXT_CELL, selected_ + 1, 0);
            return 1;
        }
        case FL_MOVE: {
            if (resizeColumn_ >= 0 && Fl::event_x() != lastX_) {
                const int delta = Fl::event_x() - lastX_;
                if (col_width(resizeColumn_) + delta > 30) {
                    col_width(resizeColumn_, col_width(resizeColumn_) + delta);
                }
                lastX_ = Fl::event_x();
                redraw();
                return 1;
            }
            break;
        }
        case FL_ENTER:
            return 1;
        case FL_RELEASE: {
            resizeColumn_ = -1;
            break;
        }
        default:
            break;
    }
    return Fl_Table_Row::handle(event);
}

}
