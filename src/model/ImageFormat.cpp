#include "model/ImageFormat.h"

#include <algorithm>
#include <cctype>

namespace lab2 {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}

const char* formatShortName(Format format) {
    switch (format) {
        case Format::Bmp:
            return "BMP";
        case Format::Png:
            return "PNG";
        case Format::Jpeg:
            return "JPEG";
        case Format::Gif:
            return "GIF";
        case Format::Tiff:
            return "TIFF";
        case Format::Pcx:
            return "PCX";
        case Format::NotImage:
            return "нет";
        default:
            return "?";
    }
}

const char* formatFullName(Format format) {
    switch (format) {
        case Format::Bmp:
            return "Windows Bitmap";
        case Format::Png:
            return "Portable Network Graphics";
        case Format::Jpeg:
            return "JPEG / JFIF";
        case Format::Gif:
            return "CompuServe GIF";
        case Format::Tiff:
            return "Tagged Image File Format";
        case Format::Pcx:
            return "ZSoft PC Paintbrush";
        case Format::NotImage:
            return "Файл не является изображением";
        default:
            return "Формат не распознан";
    }
}

std::string formatFromExtension(const std::filesystem::path& path) {
    const std::string ext = lower(path.extension().string());
    if (ext == ".bmp" || ext == ".dib") {
        return "BMP";
    }
    if (ext == ".png") {
        return "PNG";
    }
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".jpe" || ext == ".jfif") {
        return "JPEG";
    }
    if (ext == ".gif") {
        return "GIF";
    }
    if (ext == ".tif" || ext == ".tiff") {
        return "TIFF";
    }
    if (ext == ".pcx") {
        return "PCX";
    }
    return std::string();
}

bool formatIsSupported(Format format) {
    return format != Format::Unknown && format != Format::NotImage;
}

}