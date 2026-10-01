#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "model/ImageFormat.h"
#include "model/PixelStats.h"

namespace lab2 {

enum class CheckState {
    Ok,
    Warning,
    Corrupted,
    NotImage
};

struct Field {
    std::string group;
    std::string name;
    std::string value;
    std::string note;
};

struct ImageRecord {
    std::filesystem::path path;
    std::string name;
    std::string folder;
    std::uint64_t fileSize = 0;
    std::uint64_t bytesRead = 0;

    Format format = Format::Unknown;
    std::string extensionName;
    bool extensionMismatch = false;
    std::uint8_t signature[8] = {};

    int width = 0;
    int height = 0;
    bool geometryValid = false;

    int bitsPerPixel = 0;
    int sampleDepth = 0;
    int channels = 0;
    std::string colorModel;

    double dpiX = 0.0;
    double dpiY = 0.0;
    bool dpiKnown = false;
    std::string resolutionUnit;

    std::string compression;
    std::string compressionCode;

    CheckState check = CheckState::Ok;
    std::vector<std::string> problems;

    std::vector<Field> details;

    bool pixelsAnalyzed = false;
    PixelStats pixels;

    bool hasError() const { return check == CheckState::Corrupted || check == CheckState::NotImage; }
};

std::string humanSize(std::uint64_t bytes);
std::string humanDpi(double value);
std::string checkStateName(CheckState state);

}