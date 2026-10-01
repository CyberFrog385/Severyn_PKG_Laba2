#pragma once

#include <FL/Fl_Table_Row.H>

#include <string>
#include <vector>

#include "model/ImageRecord.h"

namespace lab2 {

class DetailPanel : public Fl_Table_Row {
public:
    DetailPanel(int x, int y, int w, int h, const char* label = nullptr);

    void showRecord(const ImageRecord* record);
    void showMessage(const std::string& text);

    void resize(int x, int y, int width, int height) override;

    struct Row {
        std::string group;
        std::string name;
        std::string value;
        std::string note;
    };

protected:
    void draw_cell(TableContext context, int row = 0, int column = 0, int x = 0, int y = 0,
                   int w = 0, int h = 0) override;

private:
    void applyColumnWidths();
    void updateRowHeights();
    void refreshLayout();

    std::vector<Row> rows_;
    std::vector<std::vector<std::vector<std::string>>> wrapped_;
    std::vector<std::string> messageLines_;
    std::string message_;
    bool layoutReady_ = false;
};

}