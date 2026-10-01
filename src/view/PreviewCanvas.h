#pragma once

#include <FL/Fl_Widget.H>

#include <vector>

#include "core/codec/ImageBuffer.h"

class Fl_RGB_Image;

namespace lab2 {

using ZoomCallback = void (*)(Fl_Widget*, void*);

class PreviewCanvas : public Fl_Widget {
public:
    PreviewCanvas(int x, int y, int w, int h, const char* label = nullptr);
    ~PreviewCanvas() override;

    void setBuffer(const ImageBuffer& buffer, const std::string& caption);
    void clearBuffer();

    void onZoomChanged(ZoomCallback callback, void* data) {
        zoomCallback_ = callback;
        zoomData_ = data;
    }

    void setZoom(double factor);
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomActual();
    double zoom() const { return zoom_; }
    bool fitMode() const { return fitMode_; }
    int offsetX() const { return offsetX_; }
    int offsetY() const { return offsetY_; }
    bool hasImage() const { return image_ != nullptr; }

    void draw() override;
    int handle(int event) override;
    void resize(int x, int y, int width, int height) override;

private:
    void computeTarget(int& drawWidth, int& drawHeight) const;
    void notifyZoomChanged();

    Fl_RGB_Image* image_ = nullptr;
    std::vector<unsigned char> pixels_;
    std::string caption_;
    double zoom_ = 1.0;
    bool fitMode_ = true;
    int offsetX_ = 0;
    int offsetY_ = 0;
    using ZoomCallback = void (*)(Fl_Widget*, void*);
    ZoomCallback zoomCallback_ = nullptr;
    void* zoomData_ = nullptr;
};

}