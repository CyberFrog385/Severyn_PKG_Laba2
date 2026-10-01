#pragma once

#include <filesystem>
#include <string>

namespace lab2 {

enum class Format {
    Unknown,
    NotImage,
    Bmp,
    Png,
    Jpeg,
    Gif,
    Tiff,
    Pcx
};

const char* formatShortName(Format format);
const char* formatFullName(Format format);
std::string formatFromExtension(const std::filesystem::path& path);
bool formatIsSupported(Format format);

}