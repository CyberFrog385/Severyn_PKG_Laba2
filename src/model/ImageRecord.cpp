#include "model/ImageRecord.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace lab2 {

namespace {

std::string trimNumber(double value, int digits) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(digits);
    if (std::fabs(value - std::llround(value)) < 0.005) {
        out.precision(0);
    }
    out << value;
    return out.str();
}

}

std::string humanSize(std::uint64_t bytes) {
    static const char* units[] = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(unit == 0 ? 0 : (value < 10.0 ? 2 : (value < 100.0 ? 1 : 0)));
    out << value << ' ' << units[unit];
    return out.str();
}

std::string humanDpi(double value) {
    return trimNumber(value, 2);
}

std::string checkStateName(CheckState state) {
    switch (state) {
        case CheckState::Ok:
            return "OK";
        case CheckState::Warning:
            return "предупреждение";
        case CheckState::Corrupted:
            return "файл повреждён";
        default:
            return "не изображение";
    }
}

}