#include "core/DecoderRegistry.h"

#include "core/codec/BmpDecoder.h"
#include "core/codec/GifDecoder.h"
#include "core/codec/JpegDecoder.h"
#include "core/codec/PcxDecoder.h"
#include "core/codec/PngDecoder.h"
#include "core/codec/TiffDecoder.h"

namespace lab2 {

DecodeResult decodeImage(FileHead& file, const ImageRecord& record, const DecodeLimits& limits) {
    switch (record.format) {
        case Format::Bmp:
            return decodeBmp(file, record, limits);
        case Format::Png:
            return decodePng(file, record, limits);
        case Format::Jpeg:
            return decodeJpeg(file, record, limits);
        case Format::Gif:
            return decodeGif(file, record, limits);
        case Format::Tiff:
            return decodeTiff(file, record, limits);
        case Format::Pcx:
            return decodePcx(file, record, limits);
        case Format::Unknown:
        case Format::NotImage:
        default:
            break;
    }
    DecodeResult result;
    result.error = "для этого формата нет собственного декодера";
    return result;
}

}