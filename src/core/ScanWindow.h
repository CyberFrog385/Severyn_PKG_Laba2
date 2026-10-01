#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/FileHead.h"

namespace lab2 {

class ScanWindow {
public:
    ScanWindow(FileHead& file, std::size_t blockBytes, std::size_t limitBytes)
        : file_(file), blockBytes_(blockBytes), limitBytes_(limitBytes) {}

    bool ensure(std::size_t count) {
        if (count == 0) {
            return true;
        }
        if (position_ + count <= data_.size()) {
            return true;
        }
        if (position_ > 0) {
            data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(position_));
            base_ += position_;
            position_ = 0;
        }
        while (data_.size() < count) {
            if (readTotal_ >= limitBytes_) {
                return false;
            }
            std::vector<std::uint8_t> chunk;
            if (!file_.readAt(base_ + data_.size(), blockBytes_, chunk) || chunk.empty()) {
                return false;
            }
            readTotal_ += chunk.size();
            data_.insert(data_.end(), chunk.begin(), chunk.end());
        }
        return true;
    }

    bool atEnd() const { return position_ >= data_.size(); }

    std::uint8_t peek() const { return data_[position_]; }

    std::uint8_t take() {
        if (position_ >= data_.size()) {
            return 0;
        }
        return data_[position_++];
    }

    void skip(std::size_t count) {
        position_ = std::min(position_ + count, data_.size());
    }

    bool atByte(std::uint8_t value) const {
        return position_ < data_.size() && data_[position_] == value;
    }

    bool atMarkerByte() const { return atByte(0xFF); }

    const std::uint8_t* cursor() const { return data_.data() + position_; }

    std::size_t position() const { return position_; }

    std::size_t available() const { return data_.size() - position_; }

    std::uint64_t bytesConsumed() const { return base_ + position_; }

    bool readBytes(std::size_t count, std::vector<std::uint8_t>& out) {
        if (!ensure(count)) {
            return false;
        }
        out.assign(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                   data_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
        position_ += count;
        return true;
    }

private:
    FileHead& file_;
    std::size_t blockBytes_;
    std::size_t limitBytes_;
    std::vector<std::uint8_t> data_;
    std::size_t position_ = 0;
    std::uint64_t base_ = 0;
    std::size_t readTotal_ = 0;
};

}