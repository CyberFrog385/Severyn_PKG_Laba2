#pragma once

#include <FL/Fl_Widget.H>

#include <string>

#include "model/ScanState.h"

namespace lab2 {

class StatusBar : public Fl_Widget {
public:
    StatusBar(int x, int y, int w, int h, const char* label = nullptr);

    void setSummary(const ScanSummary& summary);
    void setMessage(const std::string& text);
    void setPathText(const std::string& text);

    void draw() override;
    int handle(int event) override;

private:
    ScanSummary summary_;
    std::string message_;
    std::string path_;
    int progressX_ = 0;
    int progressY_ = 0;
    int progressW_ = 0;
    int progressH_ = 0;
};

}