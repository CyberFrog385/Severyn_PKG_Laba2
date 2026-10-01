#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>

#include <string>

#include "core/codec/ImageBuffer.h"
#include "model/ImageRecord.h"

namespace lab2 {

class PreviewCanvas;

class PreviewWindow : public Fl_Double_Window {
public:
    PreviewWindow(int w = 900, int h = 640, const char* title = "Просмотр изображения");
    ~PreviewWindow() override;

    void showRecord(const ImageRecord* record);
    void showMessage(const std::string& text);
    void present();
    bool isOpen() { return shown(); }

protected:
    void resize(int x, int y, int width, int height) override;
    int handle(int event) override;

private:
    void updateLayout();
    void setCaption(const std::string& fileName, const ImageBuffer& buffer);
    void updateZoomState();

    static void zoomInCallback(Fl_Widget* widget, void* data);
    static void zoomOutCallback(Fl_Widget* widget, void* data);
    static void zoomActualCallback(Fl_Widget* widget, void* data);
    static void zoomFitCallback(Fl_Widget* widget, void* data);
    static void zoomChangedCallback(Fl_Widget* widget, void* data);

    PreviewCanvas* canvas_ = nullptr;
    Fl_Box* zoomLabel_ = nullptr;
    Fl_Button* zoomInButton_ = nullptr;
    Fl_Button* zoomOutButton_ = nullptr;
    Fl_Button* actualButton_ = nullptr;
    Fl_Button* fitButton_ = nullptr;
};

}