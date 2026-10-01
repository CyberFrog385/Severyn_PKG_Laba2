#include "controller/CsvReport.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "model/ImageFormat.h"

namespace lab2 {

namespace {

std::string escape(const std::string& value, char separator) {
    const bool needsQuotes = value.find(separator) != std::string::npos ||
                             value.find('"') != std::string::npos ||
                             value.find('\n') != std::string::npos;
    if (!needsQuotes) {
        return value;
    }
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"') {
            quoted += '"';
        }
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

std::string number(double value, int precision = 2) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    std::string text = stream.str();
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

const char* const kHeaders[] = {
    "Файл",   "Формат",  "Расширение", "Ширина",      "Высота",   "Разрешение X",
    "Разрешение Y", "Единицы", "Бит на пиксель", "Бит на канал", "Каналы", "Модель цвета",
    "Сжатие", "Код сжатия", "Размер файла", "Прочитано байт", "Статус", "Замечания",
    "Цветов", "Энтропия", "Аргумент сжатия", "Путь"};

}

std::string toCsv(const std::vector<ImageRecord>& records, char separator) {
    std::string out;
    for (std::size_t i = 0; i < sizeof(kHeaders) / sizeof(kHeaders[0]); ++i) {
        if (i > 0) {
            out += separator;
        }
        out += escape(kHeaders[i], separator);
    }
    out += '\n';
    for (const ImageRecord& record : records) {
        std::vector<std::string> cells;
        cells.push_back(record.name);
        cells.push_back(formatFullName(record.format));
        cells.push_back(record.extensionName);
        cells.push_back(std::to_string(record.width));
        cells.push_back(std::to_string(record.height));
        cells.push_back(record.dpiKnown ? number(record.dpiX) : "не задано");
        cells.push_back(record.dpiKnown ? number(record.dpiY) : "не задано");
        cells.push_back(record.resolutionUnit);
        cells.push_back(std::to_string(record.bitsPerPixel));
        cells.push_back(std::to_string(record.sampleDepth));
        cells.push_back(std::to_string(record.channels));
        cells.push_back(record.colorModel);
        cells.push_back(record.compression);
        cells.push_back(record.compressionCode);
        cells.push_back(std::to_string(record.fileSize));
        cells.push_back(std::to_string(record.bytesRead));
        cells.push_back(checkStateName(record.check));
        std::string problems;
        for (std::size_t i = 0; i < record.problems.size(); ++i) {
            if (i > 0) {
                problems += "; ";
            }
            problems += record.problems[i];
        }
        cells.push_back(problems);
        cells.push_back(record.pixelsAnalyzed ? std::to_string(record.pixels.distinctColors) : "");
        cells.push_back(record.pixelsAnalyzed ? number(record.pixels.entropy, 3) : "");
        cells.push_back(record.pixelsAnalyzed ? number(record.pixels.compressionRatio) : "");
        cells.push_back(record.path.string());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) {
                out += separator;
            }
            out += escape(cells[i], separator);
        }
        out += '\n';
    }
    return out;
}

bool saveCsv(const std::filesystem::path& path, const std::vector<ImageRecord>& records,
             char separator) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << "\xEF\xBB\xBF";
    stream << toCsv(records, separator);
    return static_cast<bool>(stream);
}

std::string toTextTable(const std::vector<ImageRecord>& records) {
    std::ostringstream stream;
    stream << std::left << std::setw(40) << "Файл " << std::setw(9) << "Формат "
           << std::setw(13) << "Размер " << std::setw(20) << "Разрешение "
           << std::setw(9) << "Бит/пикс " << std::setw(22) << "Сжатие " << "Статус" << '\n';
    stream << std::string(120, '-') << '\n';
    for (const ImageRecord& record : records) {
        std::string geometry = record.geometryValid
                                   ? std::to_string(record.width) + " x " + std::to_string(record.height)
                                   : "н/д";
        std::string resolution =
            record.dpiKnown ? number(record.dpiX) + " x " + number(record.dpiY) : "не задано";
        std::string name = record.name;
        if (name.size() > 37) {
            name = name.substr(0, 34) + "...";
        }
        stream << std::left << std::setw(40) << name << ' ' << std::setw(8)
               << formatShortName(record.format) << ' ' << std::setw(12) << geometry << ' '
               << std::setw(19) << resolution << ' ' << std::setw(8) << record.bitsPerPixel << ' '
               << std::setw(21) << record.compression << ' ' << checkStateName(record.check)
               << '\n';
        for (const std::string& problem : record.problems) {
            stream << "    ! " << problem << '\n';
        }
    }
    return stream.str();
}

}