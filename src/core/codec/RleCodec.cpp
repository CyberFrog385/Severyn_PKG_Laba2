#include "core/codec/RleCodec.h"

#include <algorithm>
#include <utility>

namespace lab2 {

namespace {

struct RleState {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t position = 0;
    std::vector<std::uint8_t> out;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool done = false;

    bool need(std::size_t count) const { return position + count <= size; }

    void put(int value) {
        if (x >= width || y >= height) {
            ++x;
            return;
        }
        out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(value);
        ++x;
    }

    void newLine() {
        ++y;
        x = 0;
    }

    bool align() {
        if (position % 2 != 0) {
            ++position;
        }
        return position <= size;
    }
};

RleResult decodeBmpRle(RleState& state, bool nibble) {
    RleResult result;
    const std::size_t expected =
        static_cast<std::size_t>(state.width) * static_cast<std::size_t>(state.height);
    state.out.assign(expected, 0);
    while (!state.done && state.y < state.height) {
        if (!state.need(1)) {
            result.error = "поток RLE закончился до заполнения изображения";
            return result;
        }
        std::uint8_t count = state.data[state.position++];
        std::uint8_t value = 0;
        if (!state.need(1)) {
            result.error = "поток RLE закончился до заполнения изображения";
            return result;
        }
        value = state.data[state.position++];
        if (count == 0 && value == 0) {
            if (state.x == 0) {
                continue;
            }
            if (!state.align()) {
                break;
            }
            state.newLine();
            continue;
        }
        if (count > 0) {
            if (nibble) {
                const int high = value >> 4;
                const int low = value & 0x0F;
                for (int i = 0; i < count; ++i) {
                    state.put(i % 2 == 0 ? high : low);
                }
            } else {
                for (int i = 0; i < count; ++i) {
                    state.put(value);
                }
            }
            continue;
        }
        if (state.x >= state.width) {
            if (!state.align()) {
                break;
            }
            state.newLine();
            if (state.y >= state.height) {
                break;
            }
        }
        if (value == 1) {
            state.done = true;
            continue;
        }
        if (value == 2) {
            if (!state.need(2)) {
                result.error = "не найдены смещения перехода строк";
                return result;
            }
            const int dx = state.data[state.position];
            const int dy = state.data[state.position + 1];
            state.position += 2;
            state.x += dx;
            state.y += dy;
            continue;
        }
        const std::size_t pixels = value;
        const std::size_t bytes = nibble ? (pixels + 1) / 2 : pixels;
        if (!state.need(bytes)) {
            result.error = "блок абсолютных значений обрезан";
            return result;
        }
        const std::uint8_t* block = state.data + state.position;
        state.position += bytes;
        state.align();
        if (nibble) {
            for (std::size_t i = 0; i < bytes; ++i) {
                state.put(block[i] >> 4);
                if (i * 2 + 1 < pixels) {
                    state.put(block[i] & 0x0F);
                }
            }
        } else {
            for (std::size_t i = 0; i < bytes; ++i) {
                state.put(block[i]);
            }
        }
    }
    result.ok = true;
    return result;
}

}

RleResult decodeBmpRle8(const std::uint8_t* data, std::size_t size, int width, int height,
                        std::vector<std::uint8_t>& out) {
    RleState state;
    state.data = data;
    state.size = size;
    state.width = width;
    state.height = height;
    RleResult result = decodeBmpRle(state, false);
    out = std::move(state.out);
    return result;
}

RleResult decodeBmpRle4(const std::uint8_t* data, std::size_t size, int width, int height,
                        std::vector<std::uint8_t>& out) {
    RleState state;
    state.data = data;
    state.size = size;
    state.width = width;
    state.height = height;
    RleResult result = decodeBmpRle(state, true);
    out = std::move(state.out);
    return result;
}

RleResult decodePcxRle(const std::uint8_t* data, std::size_t size, std::size_t expected,
                       std::vector<std::uint8_t>& out) {
    RleResult result;
    out.clear();
    out.reserve(expected);
    std::size_t position = 0;
    while (out.size() < expected) {
        if (position >= size) {
            result.error = "поток RLE PCX закончился до заполнения изображения";
            return result;
        }
        const std::uint8_t byte = data[position++];
        if ((byte & 0xC0) == 0xC0) {
            const std::size_t count = byte & 0x3F;
            if (position >= size) {
                result.error = "повтор RLE PCX не содержит значения";
                return result;
            }
            const std::uint8_t value = data[position++];
            if (out.size() + count > expected) {
                out.resize(expected);
                result.ok = true;
                return result;
            }
            out.insert(out.end(), count, value);
            continue;
        }
        if (out.size() + 1 > expected) {
            out.resize(expected);
            result.ok = true;
            return result;
        }
        out.push_back(byte);
    }
    result.ok = true;
    return result;
}

}