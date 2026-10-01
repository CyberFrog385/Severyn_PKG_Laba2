#pragma once

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

#include "model/ImageRecord.h"

namespace lab2 {

inline constexpr double kInchPerMeter = 0.0254;

inline std::string text(long long value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string text(unsigned long long value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string text(int value) { return text(static_cast<long long>(value)); }

inline std::string text(unsigned int value) { return text(static_cast<long long>(value)); }

inline std::string number(double value, int digits = 2) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(digits);
    out << value;
    std::string result = out.str();
    const std::size_t dot = result.find('.');
    if (dot != std::string::npos) {
        while (result.size() > 0 && result.back() == '0') {
            result.pop_back();
        }
        if (!result.empty() && result.back() == '.') {
            result.pop_back();
        }
    }
    return result;
}

inline const char* yesNo(bool value) { return value ? "да" : "нет"; }

inline void addField(ImageRecord& record, std::string group, std::string name, std::string value,
                     std::string note) {
    record.details.push_back(Field{std::move(group), std::move(name), std::move(value), std::move(note)});
}

inline void addProblem(ImageRecord& record, std::string text, CheckState severity) {
    record.problems.push_back(std::move(text));
    if (severity == CheckState::Corrupted || record.check == CheckState::Ok) {
        record.check = severity;
    }
}

inline std::uint64_t bmpRowStride(int width, int bitsPerPixel) {
    const std::uint64_t rowBits = static_cast<std::uint64_t>(width) * static_cast<unsigned>(bitsPerPixel);
    return ((rowBits + 31) / 32) * 4;
}

inline std::uint64_t bytesForSamples(std::uint64_t samples, int bitsPerSample) {
    return (samples * static_cast<std::uint64_t>(bitsPerSample) + 7) / 8;
}

}