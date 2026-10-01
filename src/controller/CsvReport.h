#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "model/ImageRecord.h"

namespace lab2 {

std::string toCsv(const std::vector<ImageRecord>& records, char separator = ';');

bool saveCsv(const std::filesystem::path& path, const std::vector<ImageRecord>& records,
             char separator = ';');

std::string toTextTable(const std::vector<ImageRecord>& records);

}