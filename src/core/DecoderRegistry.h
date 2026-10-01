#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/FileHead.h"
#include "core/codec/ImageBuffer.h"
#include "model/ImageRecord.h"
#include "model/PixelStats.h"

namespace lab2 {

struct DecodeLimits {
    std::size_t maxPixels = 64u * 1024u * 1024u;
    std::size_t maxDecodedBytes = 512u * 1024u * 1024u;
    std::size_t maxCompressedBytes = 512u * 1024u * 1024u;
};

struct DecodeResult {
    bool ok = false;
    bool unsupported = false;
    bool limited = false;
    std::string decoder;
    std::string error;
    ImageBuffer buffer;
    PixelStats stats;
};

DecodeResult decodeImage(FileHead& file, const ImageRecord& record, const DecodeLimits& limits);

}