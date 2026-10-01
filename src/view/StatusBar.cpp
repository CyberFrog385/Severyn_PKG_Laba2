#include "view/StatusBar.h"

#include <FL/fl_draw.H>

#include "model/ImageRecord.h"
#include "view/PathUtil.h"

namespace lab2 {

namespace {

std::string formatSeconds(double seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f с", seconds);
    return buffer;
}

}

StatusBar::StatusBar(int x, int y, int w, int h, const char* label)
    : Fl_Widget(x, y, w, h, label) {}

void StatusBar::setSummary(const ScanSummary& summary) {
    summary_ = summary;
    redraw();
}

void StatusBar::setMessage(const std::string& text) {
    message_ = text;
    redraw();
}

void StatusBar::setPathText(const std::string& text) {
    path_ = text;
    redraw();
}

void StatusBar::draw() {
    fl_push_clip(x(), y(), w(), h());
    fl_color(fl_rgb_color(236, 238, 243));
    fl_rectf(x(), y(), w(), h());
    fl_color(fl_rgb_color(180, 184, 196));
    fl_rect(x(), y(), w(), h());

    fl_font(FL_HELVETICA, 13);
    fl_color(fl_rgb_color(40, 42, 50));
    const int lineHeight = fl_height() + 6;
    const int firstLine = y() + lineHeight - 4;

    std::string counters;
    if (summary_.total > 0) {
        counters = "Готово " + std::to_string(summary_.done) + " из " +
                   std::to_string(summary_.total) + " " + pluralFiles(summary_.total) +
                   ". Потоков: " + std::to_string(summary_.workers) + ". Время: " +
                   formatSeconds(summary_.seconds) + ". Прочитано: " + humanSize(summary_.bytesRead) +
                   " из " + humanSize(summary_.fileBytes) + ".";
    } else {
        counters = "Готов к работе. Выберите папку или файл и нажмите «Сканировать».";
    }
    if (!summary_.running && summary_.total > 0) {
        counters += " Повреждённых: " + std::to_string(summary_.corrupted) +
                    ", не изображений: " + std::to_string(summary_.notImage) +
                    ", предупреждений: " + std::to_string(summary_.warnings) + ".";
    }
    fl_draw(shortenPath(path_, 120).c_str(), x() + 8, firstLine);

    const int barTop = firstLine + 6;
    progressX_ = x() + 8;
    progressY_ = barTop;
    progressW_ = std::max(120, w() - 260);
    progressH_ = 16;
    fl_color(fl_rgb_color(214, 216, 224));
    fl_rectf(progressX_, progressY_, progressW_, progressH_);
    double ratio = 0.0;
    if (summary_.total > 0) {
        ratio = static_cast<double>(summary_.done) / static_cast<double>(summary_.total);
    }
    ratio = std::clamp(ratio, 0.0, 1.0);
    const int filled = static_cast<int>(progressW_ * ratio);
    fl_color(fl_rgb_color(60, 130, 220));
    fl_rectf(progressX_, progressY_, filled, progressH_);
    fl_color(fl_rgb_color(150, 154, 168));
    fl_rect(progressX_, progressY_, progressW_, progressH_);

    fl_color(fl_rgb_color(40, 42, 50));
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(ratio * 100));
    fl_draw(percent, progressX_ + progressW_ + 8, progressY_ + progressH_ - 4);

    if (!message_.empty()) {
        fl_color(fl_rgb_color(70, 90, 130));
        fl_draw(shortenPath(message_, 90).c_str(), progressX_ + progressW_ + 48,
                progressY_ + progressH_ - 4);
    }
    fl_pop_clip();
}

int StatusBar::handle(int event) { return Fl_Widget::handle(event); }

}