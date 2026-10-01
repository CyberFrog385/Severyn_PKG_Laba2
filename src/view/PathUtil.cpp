#include "view/PathUtil.h"

#include <algorithm>
#include <cstdio>

namespace lab2 {

std::string humanSpeed(std::uint64_t bytesPerSecond) {
    return humanSize(bytesPerSecond) + "/с";
}

std::string shortenPath(const std::filesystem::path& path, std::size_t maxLength) {
    std::string text = path.string();
    if (text.size() <= maxLength || maxLength < 4) {
        return text;
    }
    return "..." + text.substr(text.size() - (maxLength - 3));
}

std::string resolutionText(double dpi) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f x %.0f dpi", dpi, dpi);
    return buffer;
}

std::string pluralFiles(std::size_t count) {
    const std::size_t lastTwo = count % 100;
    const std::size_t last = count % 10;
    if (lastTwo >= 11 && lastTwo <= 19) {
        return "файлов";
    }
    if (last == 1) {
        return "файл";
    }
    if (last >= 2 && last <= 4) {
        return "файла";
    }
    return "файлов";
}

std::string extensionOf(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

}