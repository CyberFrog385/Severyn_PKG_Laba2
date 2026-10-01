#include "view/PreviewCanvas.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>

namespace lab2 {

namespace {

constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 16.0;
constexpr double kZoomStep = 1.25;

}

PreviewCanvas::PreviewCanvas(int x, int y, int w, int h, const char* label)
    : Fl_Widget(x, y, w, h, label) {}

PreviewCanvas::~PreviewCanvas() {
    delete image_;
    image_ = nullptr;
    pixels_.clear();
    pixels_.shrink_to_fit();
}

void PreviewCanvas::setBuffer(const ImageBuffer& buffer, const std::string& caption) {
    delete image_;
    image_ = nullptr;
    pixels_.clear();
    caption_ = caption;
    offsetX_ = 0;
    offsetY_ = 0;
    if (buffer.valid()) {
        pixels_ = buffer.pixels;
        image_ = new Fl_RGB_Image(pixels_.data(), buffer.width, buffer.height,
                                  buffer.channels, pixels_.size() / 2);
    }
    zoomFit();
    redraw();
}

void PreviewCanvas::clearBuffer() {
    delete image_;
    image_ = nullptr;
    pixels_.clear();
    pixels_.shrink_to_fit();
    caption_.clear();
    offsetX_ = 0;
    offsetY_ = 0;
    zoom_ = 1.0;
    fitMode_ = true;
    redraw();
}

void PreviewCanvas::notifyZoomChanged() {
    if (zoomCallback_ != nullptr) {
        zoomCallback_(this, zoomData_);
    }
    redraw();
}

void PreviewCanvas::setZoom(double factor) {
    zoom_ = std::clamp(factor, kMinZoom, kMaxZoom);
    fitMode_ = false;
    notifyZoomChanged();
}

void PreviewCanvas::zoomIn() { setZoom(zoom_ * kZoomStep); }

void PreviewCanvas::zoomOut() { setZoom(zoom_ / kZoomStep); }

void PreviewCanvas::zoomFit() {
    if (image_ == nullptr) {
        return;
    }
    const int availableWidth = std::max(1, w() - 8);
    const int availableHeight = std::max(1, h() - 8);
    zoom_ = std::clamp(
        std::min(static_cast<double>(availableWidth) / image_->w(),
                 static_cast<double>(availableHeight) / image_->h()),
        kMinZoom, kMaxZoom);
    fitMode_ = true;
    offsetX_ = 0;
    offsetY_ = 0;
    notifyZoomChanged();
}

void PreviewCanvas::zoomActual() {
    if (image_ == nullptr) {
        return;
    }
    setZoom(1.0);
    offsetX_ = 0;
    offsetY_ = 0;
}

void PreviewCanvas::computeTarget(int& drawWidth, int& drawHeight) const {
    if (image_ == nullptr) {
        drawWidth = 0;
        drawHeight = 0;
        return;
    }
    if (fitMode_) {
        const int availableWidth = std::max(1, w() - 8);
        const int availableHeight = std::max(1, h() - 8);
        const double scale = std::clamp(
            std::min(static_cast<double>(availableWidth) / image_->w(),
                     static_cast<double>(availableHeight) / image_->h()),
            kMinZoom, kMaxZoom);
        drawWidth = std::max(1, static_cast<int>(image_->w() * scale));
        drawHeight = std::max(1, static_cast<int>(image_->h() * scale));
    } else {
        drawWidth = std::max(1, static_cast<int>(image_->w() * zoom_));
        drawHeight = std::max(1, static_cast<int>(image_->h() * zoom_));
    }
}

void PreviewCanvas::resize(int x, int y, int width, int height) {
    const bool refit = fitMode_ && image_ != nullptr;
    Fl_Widget::resize(x, y, width, height);
    if (refit) {
        zoomFit();
    } else {
        redraw();
    }
}

void PreviewCanvas::draw() {
    fl_push_clip(x(), y(), w(), h());
    fl_color(fl_rgb_color(245, 245, 248));
    fl_rectf(x(), y(), w(), h());
    fl_color(fl_rgb_color(200, 200, 210));
    fl_rect(x(), y(), w(), h());

    if (image_ != nullptr) {
        int drawWidth = 0;
        int drawHeight = 0;
        computeTarget(drawWidth, drawHeight);
        const int drawX = x() + (w() - drawWidth) / 2 + offsetX_;
        const int drawY = y() + (h() - drawHeight) / 2 + offsetY_;
        fl_color(FL_BLACK);
        fl_rect(drawX, drawY, drawWidth, drawHeight);
        image_->draw(drawX, drawY, drawWidth, drawHeight);
        fl_font(FL_HELVETICA, 12);
        fl_color(fl_rgb_color(70, 70, 80));
        char info[96];
        std::snprintf(info, sizeof(info), "%d x %d   %d%%", image_->w(), image_->h(),
                      static_cast<int>(zoom_ * 100.0 + 0.5));
        fl_draw(info, x() + 6, y() + h() - 6);
    } else {
        fl_font(FL_HELVETICA, 13);
        fl_color(fl_rgb_color(90, 90, 90));
        const char* text = caption_.empty() ? "Изображение не выбрано" : caption_.c_str();
        fl_draw(text, x() + 10, y() + h() / 2);
    }
    fl_pop_clip();
}

int PreviewCanvas::handle(int event) {
    switch (event) {
        case FL_MOUSEWHEEL: {
            if (image_ == nullptr) {
                break;
            }
            const int steps = Fl::event_dy();
            for (int i = 0; i < std::abs(steps); ++i) {
                if (steps > 0) {
                    zoomIn();
                } else {
                    zoomOut();
                }
            }
            return 1;
        }
        case FL_MOVE: {
            if (image_ != nullptr && Fl::event_state(FL_SHIFT) != 0) {
                offsetX_ += Fl::event_dx();
                offsetY_ += Fl::event_dy();
                redraw();
                return 1;
            }
            break;
        }
        case FL_PUSH: {
            if (image_ == nullptr) {
                break;
            }
            if (Fl::event_clicks() > 0) {
                zoomFit();
                return 1;
            }
            if (Fl::event_state(FL_SHIFT) != 0) {
                return 1;
            }
            break;
        }
        default:
            break;
    }
    return Fl_Widget::handle(event);
}

}