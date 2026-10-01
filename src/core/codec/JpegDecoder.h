#pragma once

#include "core/DecoderRegistry.h"

namespace lab2 {

DecodeResult decodeJpeg(FileHead& file, const ImageRecord& record, const DecodeLimits& limits);

}