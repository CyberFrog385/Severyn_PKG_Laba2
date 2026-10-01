#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "controller/CsvReport.h"
#include "controller/ScanController.h"
#include "model/ImageFormat.h"

namespace {

void printUsage() {
    std::cout << "Laba2Scan - консольный анализатор растровых файлов\n\n"
                 "Использование: Laba2Scan [опции] <путь> [<путь> ...]\n\n"
                 "Опции:\n"
                 "  --deep          декодировать пиксели и считать статистику\n"
                 "  --csv           вывести отчёт в формате CSV вместо таблицы\n"
                 "  --save <файл>   сохранить CSV-отчёт в файл\n"
                 "  --workers <N>   число потоков (по умолчанию - по числу ядер)\n"
                 "  --no-recursive  не заходить в подпапки\n"
                 "  --quiet         не печатать таблицу, только итоги\n"
                 "  --strict        вернуть код 2, если найдены повреждённые файлы\n"
                 "  --help          эта справка\n";
}

std::string humanSize(std::uint64_t bytes) {
    static const char* const units[] = {"Б", "КБ", "МБ", "ГБ"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    return buffer;
}

}

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> targets;
    bool csv = false;
    bool deep = false;
    bool quiet = false;
    bool strict = false;
    bool recursive = true;
    int workers = 0;
    std::filesystem::path savePath;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        }
        if (argument == "--csv") {
            csv = true;
        } else if (argument == "--deep") {
            deep = true;
        } else if (argument == "--quiet") {
            quiet = true;
        } else if (argument == "--strict") {
            strict = true;
        } else if (argument == "--no-recursive") {
            recursive = false;
        } else if (argument == "--workers" && i + 1 < argc) {
            workers = std::atoi(argv[++i]);
        } else if (argument == "--save" && i + 1 < argc) {
            savePath = argv[++i];
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "Неизвестная опция: " << argument << '\n';
            return 2;
        } else {
            targets.emplace_back(argument);
        }
    }

    if (targets.empty()) {
        printUsage();
        return 2;
    }

    lab2::ScanOptions options;
    options.deepAnalysis = deep;
    options.workers = workers;
    options.recursive = recursive;
    options.extensions = {".bmp",  ".png", ".jpg", ".jpeg", ".jpe", ".gif",
                          ".tif",  ".tiff", ".pcx"};

    lab2::ScanController controller;
    controller.start(targets, options);

    while (controller.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        controller.drain();
        if (!quiet && !csv) {
            const lab2::ScanSummary summary = controller.progress();
            std::cout << "\rОбработано " << summary.done << " из " << summary.total << "  "
                      << std::flush;
        }
    }
    if (!quiet && !csv) {
        std::cout << "\r" << std::string(60, ' ') << "\r";
    }

    const std::vector<lab2::ImageRecord> records = controller.snapshot();
    const lab2::ScanSummary summary = controller.progress();

    if (!quiet) {
        if (csv) {
            std::cout << lab2::toCsv(records);
        } else {
            std::cout << lab2::toTextTable(records);
        }
    }
    if (!savePath.empty() && !lab2::saveCsv(savePath, records)) {
        std::cerr << "Не удалось сохранить отчёт: " << savePath << '\n';
        return 1;
    }

    std::cout << "\nФайлов: " << records.size() << ", прочитано " << humanSize(summary.bytesRead)
              << " из " << humanSize(summary.fileBytes) << ", время "
              << static_cast<int>(summary.seconds * 1000) / 1000.0 << " с"
              << ", потоков: " << summary.workers << '\n';
    std::cout << "Повреждённых: " << summary.corrupted << ", не изображений: " << summary.notImage
              << ", с предупреждениями: " << summary.warnings << '\n';

    std::size_t problems = 0;
    for (const lab2::ImageRecord& record : records) {
        if (record.hasError()) {
            ++problems;
        }
    }
    if (problems > 0) {
        std::cout << "Файлов с ошибками: " << problems << '\n';
    }
    if (strict && problems > 0) {
        return 2;
    }
    return 0;
}