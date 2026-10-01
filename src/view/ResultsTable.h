#pragma once

#include <FL/Fl_Table_Row.H>

#include <string>
#include <vector>

#include "model/ImageRecord.h"

namespace lab2 {

class ResultsTable : public Fl_Table_Row {
public:
    ResultsTable(int x, int y, int w, int h, const char* label = nullptr);

    void setRecords(const std::vector<ImageRecord>* records) { records_ = records; }
    void applyFilter(const std::string& text);
    const std::vector<ImageRecord>* records() const { return records_; }

    int selection() const { return selected_; }
    void setSelection(int row);
    std::size_t visibleCount() const { return visible_.size(); }
    const ImageRecord* recordAt(std::size_t visibleIndex) const;

void resize(int x, int y, int width, int height) override;

protected:
    void draw_cell(TableContext context, int row = 0, int column = 0, int x = 0, int y = 0,
                   int w = 0, int h = 0) override;
    void draw() override;
    int handle(int event) override;

private:
    int headerHeight() const;
    int rowHeight() const { return 22; }
    void rebuildVisible();
    void applyColumnWidths();

    const std::vector<ImageRecord>* records_ = nullptr;
    std::vector<std::size_t> visible_;
    std::string filter_;
    int selected_ = -1;
    int resizeColumn_ = -1;
    int lastX_ = 0;
};

}