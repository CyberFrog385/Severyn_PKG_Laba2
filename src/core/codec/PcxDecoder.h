#pragma once

#include "core/DecoderRegistry.h"

namespace lab2 {

DecodeResult decodePcx(FileHead& file, const ImageRecord& record, const DecodeLimits& limits);

}