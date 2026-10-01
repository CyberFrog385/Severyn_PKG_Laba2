#pragma once

#include "core/codec/ImageBuffer.h"
#include "model/ImageRecord.h"
#include "model/PixelStats.h"

namespace lab2 {

PixelStats analyzeRgb(const ImageBuffer& buffer, const ImageRecord& record);

}