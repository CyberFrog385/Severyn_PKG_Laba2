#include "view/MainWindow.h"

#include <cstdio>
#include <cmath>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "controller/CsvReport.h"
#include "core/DecoderRegistry.h"
#include "core/MetaExtractor.h"
#include "view/DetailPanel.h"
#include "view/FieldHelp.h"
#include "view/PathUtil.h"
#include "view/PreviewCanvas.h"
#include "view/PreviewWindow.h"
#include "view/ResultsTable.h"
#include "view/StatusBar.h"

namespace lab2 {

namespace {

constexpr int kMargin = 14;
constexpr int kRowGap = 10;
constexpr int kColumnGap = 8;
constexpr int kStatusHeight = 52;
constexpr int kWorkersLabelWidth = 62;
constexpr int kControlHeight = 28;

}

MainWindow::MainWindow() : Fl_Double_Window(1180, 760, "Анализатор растровых файлов") {
    color(fl_rgb_color(238, 240, 245));
    begin();
    buildLayout();
    end();
    resizable(table_);
    updateGeometry();
    Fl::add_timeout(0.15, timerCallback, this);
}

MainWindow::~MainWindow() {
    Fl::remove_timeout(timerCallback, this);
    delete previewWindow_;
    previewWindow_ = nullptr;
}

void MainWindow::folderCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onChooseFolder(); }
void MainWindow::fileCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onChooseFile(); }
void MainWindow::scanCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onScan(); }
void MainWindow::stopCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onStop(); }
void MainWindow::exportCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onExport(); }
void MainWindow::aboutCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onAbout(); }
void MainWindow::deepCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onDeepAnalysis(); }
void MainWindow::workersCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onWorkers(); }
void MainWindow::filterCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onFilter(); }
void MainWindow::previewCallback(Fl_Widget*, void* data) { static_cast<MainWindow*>(data)->onPreview(); }


void MainWindow::buildLayout() {
    folderButton_ = new Fl_Button(0, 0, 0, 0, "Папка...");
    folderButton_->callback(folderCallback, this);
    fileButton_ = new Fl_Button(0, 0, 0, 0, "Файлы...");
    fileButton_->callback(fileCallback, this);
    scanButton_ = new Fl_Button(0, 0, 0, 0, "Сканировать");
    scanButton_->callback(scanCallback, this);
    stopButton_ = new Fl_Button(0, 0, 0, 0, "Стоп");
    stopButton_->callback(stopCallback, this);
    stopButton_->deactivate();
    exportButton_ = new Fl_Button(0, 0, 0, 0, "Экспорт CSV");
    exportButton_->callback(exportCallback, this);
    aboutButton_ = new Fl_Button(0, 0, 0, 0, "Справка");
    aboutButton_->callback(aboutCallback, this);

    deepCheck_ = new Fl_Check_Button(0, 0, 0, 0, "Глубокий анализ");
    deepCheck_->callback(deepCallback, this);
    recursiveCheck_ = new Fl_Check_Button(0, 0, 0, 0, "Подпапки");
    recursiveCheck_->value(1);

    workersLabel_ = new Fl_Box(0, 0, 0, 0, "Потоки:");
    workersLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    workersLabel_->labelfont(FL_HELVETICA);
    workersLabel_->labelsize(13);
    workersLabel_->labelcolor(fl_rgb_color(70, 70, 80));

    workersChoice_ = new Fl_Choice(0, 0, 0, 0);
    const int recommended = recommendedWorkerCount();
    for (int i = 1; i <= 16; ++i) {
        workersChoice_->add(std::to_string(i).c_str());
    }
    workersChoice_->value(recommended - 1);
    workersChoice_->callback(workersCallback, this);

    filterLabel_ = new Fl_Box(0, 0, 0, 0, "Фильтр:");
    filterLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    filterLabel_->labelfont(FL_HELVETICA);
    filterLabel_->labelsize(13);
    filterLabel_->labelcolor(fl_rgb_color(70, 70, 80));

    filterInput_ = new Fl_Input(0, 0, 0, 0);
    filterInput_->when(FL_WHEN_CHANGED);
    filterInput_->callback(filterCallback, this);

    resultsLabel_ = new Fl_Box(0, 0, 0, 0, "Результаты");
    resultsLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    resultsLabel_->labelfont(FL_HELVETICA_BOLD);
    resultsLabel_->labelsize(14);
    resultsLabel_->labelcolor(fl_rgb_color(40, 40, 50));

    table_ = new ResultsTable(0, 0, 0, 0);
    table_->callback(selectionCallback, this);

    detailsLabel_ = new Fl_Box(0, 0, 0, 0, "Свойства файла");
    detailsLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    detailsLabel_->labelfont(FL_HELVETICA_BOLD);
    detailsLabel_->labelsize(14);
    detailsLabel_->labelcolor(fl_rgb_color(40, 40, 50));

    details_ = new DetailPanel(0, 0, 0, 0);

    previewButton_ = new Fl_Button(0, 0, 0, 0, "Просмотр");
    previewButton_->callback(previewCallback, this);
    previewButton_->deactivate();

    status_ = new StatusBar(0, 0, 0, 0);
}

void MainWindow::updateGeometry() {
    const int width = std::max(820, this->w());
    const int height = std::max(640, this->h());
    const int statusTop = height - kStatusHeight;
    const int fullWidth = width - 2 * kMargin;

    int y = kMargin;
    int x = kMargin;

    folderButton_->resize(x, y, 96, kControlHeight);
    x += 96 + kColumnGap;
    fileButton_->resize(x, y, 96, kControlHeight);
    x += 96 + kColumnGap;
    scanButton_->resize(x, y, 116, kControlHeight);
    x += 116 + kColumnGap;
    stopButton_->resize(x, y, 78, kControlHeight);
    x += 78 + kColumnGap;
    exportButton_->resize(x, y, 112, kControlHeight);
    x += 112 + kColumnGap * 2;

    deepCheck_->resize(x, y, 144, kControlHeight);
    x += 144 + kColumnGap;
    recursiveCheck_->resize(x, y, 104, kControlHeight);

    y += kControlHeight + kRowGap;
    x = kMargin;

    workersLabel_->resize(x, y, kWorkersLabelWidth, kControlHeight);
    workersChoice_->resize(x + kWorkersLabelWidth, y, 78, kControlHeight);

    const int filterLabelX = x + kWorkersLabelWidth + 78 + kColumnGap * 3;
    fl_font(FL_HELVETICA, 14);
    const int filterLabelWidth = static_cast<int>(fl_width("Фильтр:")) + 8;
    filterLabel_->resize(filterLabelX, y, filterLabelWidth, kControlHeight);

    const int filterInputX = filterLabelX + filterLabelWidth + 4;
    const int rightButtonsWidth = 96 + kColumnGap + 96;
    const int filterWidth = std::max(
        180, width - filterInputX - rightButtonsWidth - kMargin - kColumnGap);
    filterInput_->resize(filterInputX, y, filterWidth, kControlHeight);

    const int aboutX = filterInputX + filterWidth + kColumnGap;
    aboutButton_->resize(aboutX, y, 96, kControlHeight);
    previewButton_->resize(aboutX + 96 + kColumnGap, y, 96, kControlHeight);

    const int contentTop = y + kControlHeight + kRowGap;
    const int contentHeight = statusTop - contentTop - kMargin;
    const int labelHeight = 24;
    const int sectionGap = 8;

    const int detailsHeight = std::max(220, contentHeight / 3);
    const int detailsLabelTop = contentTop + contentHeight - detailsHeight;
    const int tableTop = contentTop + labelHeight + 4;
    const int tableHeight = detailsLabelTop - sectionGap - tableTop;

    resultsLabel_->resize(kMargin, contentTop, fullWidth, labelHeight);
    table_->resize(kMargin, tableTop, fullWidth, std::max(120, tableHeight));
    detailsLabel_->resize(kMargin, detailsLabelTop, fullWidth, labelHeight);
    details_->resize(kMargin, detailsLabelTop + labelHeight, fullWidth,
                     std::max(120, detailsHeight - labelHeight));
    status_->resize(kMargin, statusTop, fullWidth, kStatusHeight - 6);
}

void MainWindow::onChooseFolder() {
    const char* chosen = fl_input("Укажите путь к папке с изображениями:", lastPath_.c_str());
    if (chosen == nullptr || *chosen == '\0') {
        return;
    }
    std::error_code error;
    const std::filesystem::path path(chosen);
    if (!std::filesystem::is_directory(path, error)) {
        fl_alert("Указанный путь не является папкой.");
        return;
    }
    lastPath_ = chosen;
    targets_ = {path};
    status_->setPathText(path.string());
    status_->setMessage("Папка выбрана. Нажмите «Сканировать».");
}

void MainWindow::onChooseFile() {
    const char* pattern = "Все файлы\t*.*\t"
                          "Изображения\t*.{bmp,png,jpg,jpeg,gif,tif,tiff,pcx}\t"
                          "BMP\t*.bmp\tPNG\t*.png\tJPEG\t*.{jpg,jpeg}\t"
                          "GIF\t*.gif\tTIFF\t*.{tif,tiff}\tPCX\t*.pcx";
    const char* chosen = fl_file_chooser("Выберите файлы изображений", pattern,
                                         lastPath_.empty() ? nullptr : lastPath_.c_str());
    if (chosen == nullptr || *chosen == '\0') {
        return;
    }
    std::error_code error;
    std::vector<std::filesystem::path> targets;
    if (std::filesystem::is_directory(chosen, error)) {
        targets.emplace_back(chosen);
    } else {
        targets.emplace_back(chosen);
    }
    lastPath_ = chosen;
    targets_ = targets;
    status_->setPathText(chosen);
    status_->setMessage("Файл выбран. Нажмите «Сканировать».");
}

void MainWindow::onScan() {
    if (targets_.empty()) {
        fl_alert("Сначала выберите папку или файл.");
        return;
    }
    records_.clear();
    table_->setRecords(&records_);
    table_->applyFilter(filterInput_->value());
    details_->showMessage("Файл не выбран");
    previewButton_->deactivate();
    startScan(targets_);
}

void MainWindow::onStop() {
    controller_.cancel();
    status_->setMessage("Остановка по запросу пользователя.");
}

void MainWindow::startScan(const std::vector<std::filesystem::path>& targets) {
    ScanOptions options;
    options.deepAnalysis = deepCheck_->value() != 0;
    options.recursive = recursiveCheck_->value() != 0;
    options.workers = workersChoice_->value() + 1;
    options.extensions = {".bmp", ".png", ".jpg", ".jpeg", ".jpe", ".gif", ".tif", ".tiff", ".pcx"};
    scanButton_->deactivate();
    stopButton_->activate();
    status_->setMessage("Идёт анализ...");
    controller_.start(targets, options);
}

void MainWindow::onExport() {
    if (records_.empty()) {
        fl_alert("Нет результатов для экспорта.");
        return;
    }
    const char* chosen = fl_file_chooser("Сохранить отчёт CSV", "CSV\t*.csv", nullptr);
    if (chosen == nullptr || *chosen == '\0') {
        return;
    }
    std::string path = chosen;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".csv") {
        path += ".csv";
    }
    if (saveCsv(path, records_)) {
        status_->setMessage("Отчёт сохранён: " + path);
    } else {
        fl_alert("Не удалось сохранить отчёт.");
    }
}

void MainWindow::onDeepAnalysis() {
    status_->setMessage(deepCheck_->value() != 0
                            ? "Глубокий анализ включён: будут декодированы пиксели всех изображений."
                            : "Глубокий анализ выключен: читаются только заголовки файлов.");
}

void MainWindow::onWorkers() {
    status_->setMessage("Число потоков: " +
                        std::to_string(workersChoice_->value() + 1) + ".");
}

void MainWindow::onFilter() { table_->applyFilter(filterInput_->value()); }

void MainWindow::onSelectionChanged() {
    showSelected();
    const bool hasRecord = table_->selection() >= 0;
    if (hasRecord) {
        previewButton_->activate();
    } else {
        previewButton_->deactivate();
    }
}

void MainWindow::onPreview() {
    if (previewWindow_ == nullptr) {
        previewWindow_ = new PreviewWindow();
    }
    const ImageRecord* record =
        table_->recordAt(static_cast<std::size_t>(table_->selection()));
    previewWindow_->showRecord(record);
    status_->setMessage("Окно просмотра открыто.");
}

void MainWindow::onAbout() {
    const std::string text =
        std::string("Анализатор растровых файлов\n"
                    "Поддерживаемые форматы: BMP, PNG, JPEG, GIF, TIFF, PCX.\n"
                    "Все заголовки разбираются собственными парсерами, пиксели "
                    "восстанавливаются собственными декодерами.\n\n"
                    "Что означают поля:\n") +
        columnHelp(0) + "\n" + columnHelp(3) + "\n" + columnHelp(5) + "\n" + statusHelp();
    fl_message("%s", text.c_str());
}

void MainWindow::showSelected() {
    const ImageRecord* record = table_->recordAt(static_cast<std::size_t>(table_->selection()));
    if (record == nullptr) {
        details_->showMessage("Файл не выбран");
        return;
    }
    details_->showRecord(record);
}

void MainWindow::updateStatus() {
    const ScanSummary summary = controller_.progress();
    status_->setSummary(summary);
    const bool running = controller_.running();
    scanButton_->value(0);
    if (running) {
        scanButton_->deactivate();
        stopButton_->activate();
    } else {
        scanButton_->activate();
        stopButton_->deactivate();
    }
}

void MainWindow::timerCallback(void* data) {
    auto* window = static_cast<MainWindow*>(data);
    window->poll();
    Fl::repeat_timeout(0.15, timerCallback, data);
}

void MainWindow::selectionCallback(Fl_Widget* widget, void* data) {
    (void)widget;
    static_cast<MainWindow*>(data)->onSelectionChanged();
}

void MainWindow::poll() {
    if (lastWidth_ != w() || lastHeight_ != h()) {
        lastWidth_ = w();
        lastHeight_ = h();
        updateGeometry();
    }
    std::vector<ImageRecord> batch = controller_.drain();
    if (!batch.empty()) {
        for (ImageRecord& record : batch) {
            records_.push_back(std::move(record));
        }
        std::sort(records_.begin(), records_.end(),
                  [](const ImageRecord& a, const ImageRecord& b) { return a.name < b.name; });
        table_->setRecords(&records_);
        table_->applyFilter(filterInput_->value());
    }
    updateStatus();
}

}