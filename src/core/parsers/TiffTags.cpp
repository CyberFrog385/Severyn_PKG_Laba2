#include "core/parsers/TiffTags.h"

#include <cstddef>

namespace lab2 {

namespace {

struct TagEntry {
    std::uint16_t id;
    const char* name;
};

const TagEntry kTags[] = {
    {254, "NewSubfileType"},
    {255, "SubfileType"},
    {256, "ImageWidth"},
    {257, "ImageLength"},
    {258, "BitsPerSample"},
    {259, "Compression"},
    {262, "PhotometricInterpretation"},
    {263, "Threshholding"},
    {264, "CellWidth"},
    {265, "CellLength"},
    {266, "FillOrder"},
    {269, "DocumentName"},
    {270, "ImageDescription"},
    {271, "Make"},
    {272, "Model"},
    {273, "StripOffsets"},
    {274, "Orientation"},
    {277, "SamplesPerPixel"},
    {278, "RowsPerStrip"},
    {279, "StripByteCounts"},
    {280, "MinSampleValue"},
    {281, "MaxSampleValue"},
    {282, "XResolution"},
    {283, "YResolution"},
    {284, "PlanarConfiguration"},
    {285, "PageName"},
    {286, "XPosition"},
    {287, "YPosition"},
    {288, "FreeOffsets"},
    {289, "FreeByteCounts"},
    {296, "ResolutionUnit"},
    {297, "PageNumber"},
    {301, "TransferFunction"},
    {305, "Software"},
    {306, "DateTime"},
    {315, "Artist"},
    {316, "HostComputer"},
    {317, "Predictor"},
    {318, "WhitePoint"},
    {319, "PrimaryChromaticities"},
    {320, "ColorMap"},
    {321, "HalftoneHints"},
    {322, "TileWidth"},
    {323, "TileLength"},
    {324, "TileOffsets"},
    {325, "TileByteCounts"},
    {330, "SubIFDs"},
    {338, "ExtraSamples"},
    {339, "SampleFormat"},
    {33432, "Copyright"},
    {33723, "IPTC"},
    {34665, "ExifIFD"},
    {34675, "InterColorProfile"},
    {34677, "ImageLayer"},
    {34735, "GeoKeyDirectory"},
    {34736, "GeoDoubleParams"},
    {34737, "GeoAsciiParams"},
    {34850, "ExposureProgram"},
    {34853, "GPSIFD"},
    {34908, "HylaFAXFaxRecvParams"},
    {37724, "ImageSourceData"},
    {40091, "XPTitle"},
    {40092, "XPComment"},
    {40093, "XPAuthor"},
    {40094, "XPKeywords"},
    {40095, "XPSubject"},
    {42112, "GDAL_METADATA"},
    {42113, "GDAL_NODATA"},
    {50341, "PrintImageMatching"},
    {50706, "DNGVersion"},
};

}

const char* tiffTagName(std::uint16_t id) {
    for (const TagEntry& entry : kTags) {
        if (entry.id == id) {
            return entry.name;
        }
    }
    return nullptr;
}

std::uint32_t tiffTypeSize(std::uint16_t type) {
    switch (type) {
        case 1:
        case 2:
        case 6:
        case 7:
            return 1;
        case 3:
        case 8:
            return 2;
        case 4:
        case 9:
        case 11:
            return 4;
        case 5:
        case 10:
        case 12:
            return 8;
        case 16:
        case 17:
        case 18:
            return 8;
        default:
            return 0;
    }
}

const char* tiffTypeName(std::uint16_t type) {
    switch (type) {
        case 1:
            return "BYTE";
        case 2:
            return "ASCII";
        case 3:
            return "SHORT";
        case 4:
            return "LONG";
        case 5:
            return "RATIONAL";
        case 6:
            return "SBYTE";
        case 7:
            return "UNDEFINED";
        case 8:
            return "SSHORT";
        case 9:
            return "SLONG";
        case 10:
            return "SRATIONAL";
        case 11:
            return "FLOAT";
        case 12:
            return "DOUBLE";
        case 16:
            return "LONG8";
        case 17:
            return "SLONG8";
        case 18:
            return "IFD8";
        default:
            return "неизвестный";
    }
}

const char* tiffCompressionName(std::uint16_t code) {
    switch (code) {
        case 1:
            return "Без сжатия (none)";
        case 2:
            return "CCITT Group 3 ( fax )";
        case 3:
            return "CCITT T.4";
        case 4:
            return "CCITT T.6";
        case 5:
            return "LZW";
        case 6:
            return "JPEG ( старый )";
        case 7:
            return "JPEG";
        case 8:
            return "Adobe Deflate (zlib)";
        case 32773:
            return "PackBits";
        case 32946:
            return "Deflate (zlib)";
        case 34676:
            return "JBIG";
        case 34677:
            return "JPEG 2000";
        case 34712:
            return "JPEG 2000";
        case 34713:
            return "JPEG XL";
        case 34925:
            return "LZMA";
        case 50000:
            return "Zstandard";
        case 50001:
            return "WebP";
        default:
            return "неизвестный код";
    }
}

const char* tiffPhotometricName(std::uint16_t code) {
    switch (code) {
        case 0:
            return "Белое — ноль (grayscale, WhiteIsZero)";
        case 1:
            return "Чёрное — ноль (grayscale, BlackIsZero)";
        case 2:
            return "RGB";
        case 3:
            return "Палитра";
        case 4:
            return "Прозрачная маска";
        case 5:
            return "CMYK";
        case 6:
            return "YCbCr";
        case 8:
            return "CIELab";
        default:
            return "неизвестный код";
    }
}

const char* tiffResolutionUnitName(std::uint16_t code) {
    switch (code) {
        case 1:
            return "нет (соотношение сторон)";
        case 2:
            return "дюйм";
        case 3:
            return "сантиметр";
        default:
            return "не задано";
    }
}

const char* tiffPredictorName(std::uint16_t code) {
    switch (code) {
        case 1:
            return "нет";
        case 2:
            return "горизонтальное дифференцирование";
        case 3:
            return "планарное";
        default:
            return "неизвестный код";
    }
}

}