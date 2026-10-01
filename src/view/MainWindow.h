#pragma once

#include <FL/Fl_Double_Window.H>

#include <filesystem>
#include <string>
#include <vector>

#include "controller/ScanController.h"
#include "model/ImageRecord.h"

class Fl_Box;
class Fl_Button;
class Fl_Check_Button;
class Fl_Choice;
class Fl_Input;

namespace lab2 {

class PreviewWindow;
class ResultsTable;
class DetailPanel;
class StatusBar;

class MainWindow : public Fl_Double_Window {
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void buildLayout();
    void updateGeometry();

    void onChooseFolder();
    void onChooseFile();
    void onScan();
    void onStop();
    void onExport();
    void onDeepAnalysis();
    void onWorkers();
    void onFilter();
    void onSelectionChanged();
    void onPreview();
    void onAbout();

    static void timerCallback(void* data);
    static void selectionCallback(Fl_Widget* widget, void* data);
    static void folderCallback(Fl_Widget* widget, void* data);
    static void fileCallback(Fl_Widget* widget, void* data);
    static void scanCallback(Fl_Widget* widget, void* data);
    static void stopCallback(Fl_Widget* widget, void* data);
    static void exportCallback(Fl_Widget* widget, void* data);
    static void aboutCallback(Fl_Widget* widget, void* data);
    static void deepCallback(Fl_Widget* widget, void* data);
static void workersCallback(Fl_Widget* widget, void* data);
    static void filterCallback(Fl_Widget* widget, void* data);
    static void previewCallback(Fl_Widget* widget, void* data);

private:
    void poll();
    void startScan(const std::vector<std::filesystem::path>& targets);
    void showSelected();
    void updateStatus();

    ScanController controller_;
    std::vector<ImageRecord> records_;
    std::vector<std::filesystem::path> targets_;
    std::string lastPath_;

    Fl_Button* folderButton_ = nullptr;
    Fl_Button* fileButton_ = nullptr;
    Fl_Button* scanButton_ = nullptr;
    Fl_Button* stopButton_ = nullptr;
    Fl_Button* exportButton_ = nullptr;
    Fl_Button* aboutButton_ = nullptr;
    Fl_Button* previewButton_ = nullptr;
    Fl_Check_Button* deepCheck_ = nullptr;
    Fl_Check_Button* recursiveCheck_ = nullptr;
    Fl_Choice* workersChoice_ = nullptr;
    Fl_Box* workersLabel_ = nullptr;
    Fl_Box* filterLabel_ = nullptr;
    Fl_Input* filterInput_ = nullptr;
    Fl_Box* resultsLabel_ = nullptr;
    Fl_Box* detailsLabel_ = nullptr;

    ResultsTable* table_ = nullptr;
    DetailPanel* details_ = nullptr;
    StatusBar* status_ = nullptr;
    PreviewWindow* previewWindow_ = nullptr;

    int lastWidth_ = 0;
    int lastHeight_ = 0;
};

}