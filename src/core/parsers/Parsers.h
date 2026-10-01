#pragma once

#include "core/FileHead.h"
#include "model/ImageRecord.h"

namespace lab2 {

bool parseBmp(FileHead& file, ImageRecord& record);
bool parsePng(FileHead& file, ImageRecord& record);
bool parseJpeg(FileHead& file, ImageRecord& record);
bool parseGif(FileHead& file, ImageRecord& record);
bool parseTiff(FileHead& file, ImageRecord& record);
bool parsePcx(FileHead& file, ImageRecord& record);

}