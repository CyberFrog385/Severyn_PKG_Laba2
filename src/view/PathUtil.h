#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "model/ImageRecord.h"

namespace lab2 {

std::string humanSpeed(std::uint64_t bytesPerSecond);
std::string shortenPath(const std::filesystem::path& path, std::size_t maxLength);
std::string resolutionText(double dpi);
std::string pluralFiles(std::size_t count);
std::string extensionOf(const std::filesystem::path& path);

}