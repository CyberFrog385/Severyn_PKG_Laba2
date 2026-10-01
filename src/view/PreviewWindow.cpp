#include "view/PreviewWindow.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>

#include "core/DecoderRegistry.h"
#include "core/FileHead.h"
#include "view/PreviewCanvas.h"

namespace lab2 {

namespace {

constexpr int kBarHeight = 34;
constexpr int kBarMargin = 8;
constexpr int kButtonWidth = 96;
constexpr int kButtonHeight = 26;

}

PreviewWindow::PreviewWindow(int width, int height, const char* title)
    : Fl_Double_Window(width, height, title) {
    color(fl_rgb_color(238, 240, 245));
    begin();

    canvas_ = new PreviewCanvas(0, 0, width, height - kBarHeight);
    canvas_->onZoomChanged(zoomChangedCallback, this);

    zoomOutButton_ = new Fl_Button(0, 0, 0, 0, "\xe2\x80\x93");
    zoomOutButton_->callback(zoomOutCallback, this);
    zoomOutButton_->tooltip("Уменьшить");

    zoomInButton_ = new Fl_Button(0, 0, 0, 0, "+");
    zoomInButton_->callback(zoomInCallback, this);
    zoomInButton_->tooltip("Увеличить");

    actualButton_ = new Fl_Button(0, 0, 0, 0, "100%");
    actualButton_->callback(zoomActualCallback, this);
    actualButton_->tooltip("Исходный размер");

    fitButton_ = new Fl_Button(0, 0, 0, 0, "Вписать");
    fitButton_->callback(zoomFitCallback, this);
    fitButton_->tooltip("Вписать в окно");

    zoomLabel_ = new Fl_Box(0, 0, 0, 0, "");
    zoomLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    zoomLabel_->labelsize(12);

    end();
    resizable(canvas_);
    size_range(520, 340);
    updateLayout();
    copy_label(title);
    hide();
}

PreviewWindow::~PreviewWindow() = default;

void PreviewWindow::resize(int x, int y, int width, int height) {
    Fl_Double_Window::resize(x, y, width, height);
    updateLayout();
}

void PreviewWindow::updateLayout() {
    if (canvas_ == nullptr) {
        return;
    }
    const int barY = std::max(0, h() - kBarHeight);
    canvas_->resize(0, 0, std::max(1, w()), barY);

    int x = kBarMargin;
    const int buttonY = barY + (kBarHeight - kButtonHeight) / 2;
    zoomOutButton_->resize(x, buttonY, kButtonHeight, kButtonHeight);
    x += kButtonHeight + kBarMargin / 2;
    zoomInButton_->resize(x, buttonY, kButtonHeight, kButtonHeight);
    x += kButtonHeight + kBarMargin;
    actualButton_->resize(x, buttonY, kButtonWidth, kButtonHeight);
    x += kButtonWidth + kBarMargin;
    fitButton_->resize(x, buttonY, kButtonWidth, kButtonHeight);
    x += kButtonWidth + kBarMargin;
    zoomLabel_->resize(x, buttonY, std::max(60, w() - x - kBarMargin), kButtonHeight);

    updateZoomState();
    redraw();
}

void PreviewWindow::updateZoomState() {
    if (canvas_ == nullptr || zoomLabel_ == nullptr) {
        return;
    }
    const bool hasImage = canvas_->hasImage();
    if (hasImage) {
        zoomInButton_->activate();
        zoomOutButton_->activate();
        actualButton_->activate();
        fitButton_->activate();
    } else {
        zoomInButton_->deactivate();
        zoomOutButton_->deactivate();
        actualButton_->deactivate();
        fitButton_->deactivate();
    }
    if (!hasImage) {
        copy_label(zoomLabel_->label());
        zoomLabel_->copy_label("");
        return;
    }
    char info[64];
    std::snprintf(info, sizeof(info), "%d%%%s",
                  static_cast<int>(canvas_->zoom() * 100.0 + 0.5),
                  canvas_->fitMode() ? " (вписано)" : "");
    zoomLabel_->copy_label(info);
}

void PreviewWindow::zoomInCallback(Fl_Widget*, void* data) {
    static_cast<PreviewWindow*>(data)->canvas_->zoomIn();
    static_cast<PreviewWindow*>(data)->updateZoomState();
}

void PreviewWindow::zoomOutCallback(Fl_Widget*, void* data) {
    static_cast<PreviewWindow*>(data)->canvas_->zoomOut();
    static_cast<PreviewWindow*>(data)->updateZoomState();
}

void PreviewWindow::zoomActualCallback(Fl_Widget*, void* data) {
    static_cast<PreviewWindow*>(data)->canvas_->zoomActual();
    static_cast<PreviewWindow*>(data)->updateZoomState();
}

void PreviewWindow::zoomFitCallback(Fl_Widget*, void* data) {
    static_cast<PreviewWindow*>(data)->canvas_->zoomFit();
    static_cast<PreviewWindow*>(data)->updateZoomState();
}

int PreviewWindow::handle(int event) {
    if (event == FL_HIDE && canvas_ != nullptr) {
        canvas_->redraw();
    }
    if (event == FL_ENTER && canvas_ != nullptr) {
        canvas_->take_focus();
    }
    return Fl_Double_Window::handle(event);
}

void PreviewWindow::setCaption(const std::string& fileName, const ImageBuffer& buffer) {
    std::string caption = fileName.empty() ? "Просмотр изображения" : fileName;
    if (buffer.valid()) {
        char size[48];
        std::snprintf(size, sizeof(size), " — %d x %d px", buffer.width, buffer.height);
        caption += size;
    }
    copy_label(caption.c_str());
}

void PreviewWindow::showMessage(const std::string& text) {
    ImageBuffer empty;
    canvas_->setBuffer(empty, text);
    setCaption("", empty);
    present();
}

void PreviewWindow::showRecord(const ImageRecord* record) {
    if (record == nullptr) {
        showMessage("Файл не выбран");
        return;
    }
    if (!record->pixelsAnalyzed || !record->pixels.decoded) {
        ImageBuffer empty;
        canvas_->setBuffer(empty, "Включите глубокий анализ, чтобы увидеть изображение.");
        setCaption(record->name, empty);
        present();
        return;
    }
    FileHead file;
    if (!file.open(record->path)) {
        ImageBuffer empty;
        canvas_->setBuffer(empty, "Не удалось открыть файл повторно.");
        setCaption(record->name, empty);
        present();
        return;
    }
    ImageRecord copy = *record;
    const DecodeLimits limits;
    const DecodeResult decoded = decodeImage(file, copy, limits);
    if (decoded.ok) {
        canvas_->setBuffer(decoded.buffer, "");
        setCaption(record->name, decoded.buffer);
    } else {
        ImageBuffer empty;
        canvas_->setBuffer(empty, decoded.error);
        setCaption(record->name, empty);
    }
    present();
}

void PreviewWindow::present() {
    if (!shown()) {
        show();
    }
    updateZoomState();
    Fl::flush();
    canvas_->take_focus();
    canvas_->redraw();
}

void PreviewWindow::zoomChangedCallback(Fl_Widget*, void* data) {
    static_cast<PreviewWindow*>(data)->updateZoomState();
}

}