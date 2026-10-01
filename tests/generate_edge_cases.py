"""Генератор дополнительных проверочных файлов tests/data и tests/corrupt.

Запуск:  python3 tests/generate_edge_cases.py [каталог]

Создаёт файлы, которые Pillow не умеет записать сам (BMP с RLE4/RLE8,
PNG с tRNS для типов цвета 0 и 2), а также обрезанные копии обычных
изображений для проверки сообщений о повреждении.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data"
CORRUPT = ROOT / "corrupt"


def emit_rle(rows: list[list[int]], width: int, nibble: bool) -> bytes:
    """Кодирует строки BMP RLE, сохраняя сквозное выравнивание до границы слова."""
    out = bytearray()
    maxRun = 255

    def runAt(values: list[int], pos: int) -> int:
        length = 1
        while (pos + length < width and values[pos + length] == values[pos]
               and length < maxRun):
            length += 1
        return length

    def emitLiteral(pixels: list[int]) -> None:
        if not pixels:
            return
        if nibble:
            block = bytearray()
            for i in range(0, len(pixels), 2):
                block.append((pixels[i] << 4) | (pixels[i + 1] if i + 1 < len(pixels)
                                                    else pixels[i]))
        else:
            block = bytearray(pixels)
        out.append(0)
        out.append(len(pixels))
        out.extend(block)
        if len(out) % 2:
            out.append(0)

    def emitRun(count: int, value: int) -> None:
        if nibble:
            out.append(count)
            out.append((value << 4) | value)
        else:
            out.append(count)
            out.append(value)

    def emitGroup(pixels: list[int]) -> None:
        if not pixels:
            return
        if nibble:
            count = len(pixels) - len(pixels) % 2
            if count >= 4:
                emitLiteral(pixels[:count])
                for pixel in pixels[count:]:
                    emitRun(1, pixel)
                return
            for pixel in pixels:
                emitRun(1, pixel)
            return
        if len(pixels) >= 3:
            emitLiteral(pixels)
            return
        for pixel in pixels:
            emitRun(1, pixel)

    limit = 254 if nibble else 255
    for values in rows:
        pending: list[int] = []
        x = 0
        while x < width:
            run = runAt(values, x)
            if run >= 2:
                emitGroup(pending)
                pending.clear()
                emitRun(run, values[x])
                x += run
                continue
            if len(pending) >= limit:
                emitGroup(pending)
                pending.clear()
            pending.append(values[x])
            x += 1
        emitGroup(pending)
        out += bytes([0, 0])
        if len(out) % 2:
            out.append(0)
    return bytes(out)


def write_bmp_palette(path: Path, width: int, height: int, compression: int,
                      rows: list[list[int]], palette: list[tuple[int, int, int]],
                      bits: int) -> None:
    stream = emit_rle([rows[y] for y in range(height - 1, -1, -1)], width, bits == 4)
    stream += bytes([0, 1])
    palette_bytes = b"".join(bytes([b, g, r, 0]) for (r, g, b) in palette)
    offset = 14 + 40 + len(palette_bytes)
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, bits, compression, len(stream),
                      2835, 2835, len(palette), 0)
    file_header = struct.pack("<2sIHHI", b"BM", offset + len(stream), 0, 0, offset)
    path.write_bytes(file_header + dib + palette_bytes + stream)


def write_bmp_rle8_stream(path: Path, width: int, height: int,
                          palette: list[tuple[int, int, int]], stream: bytes) -> None:
    palette_bytes = b"".join(bytes([b, g, r, 0]) for (r, g, b) in palette)
    offset = 14 + 40 + len(palette_bytes)
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 8, 1, len(stream), 2835, 2835,
                      len(palette), 0)
    file_header = struct.pack("<2sIHHI", b"BM", offset + len(stream), 0, 0, offset)
    path.write_bytes(file_header + dib + palette_bytes + stream)


def palette_image(rows: list[list[int]], palette: list[tuple[int, int, int]]) -> Image.Image:
    height = len(rows)
    width = len(rows[0])
    image = np.zeros((height, width, 3), dtype=np.uint8)
    for y in range(height):
        for x in range(width):
            image[y, x] = palette[rows[y][x]]
    return Image.fromarray(image, "RGB")


def build_bmp_rle(data: Path) -> None:
    rng = np.random.default_rng(7)
    palette4 = [(i * 7 % 256, (255 - i * 5) % 256, (i * 13) % 256) for i in range(16)]
    rows4 = [[int(v) for v in rng.integers(0, 16, size=30)] for _ in range(20)]
    write_bmp_palette(data / "bmp_rle4_30x20.bmp", 30, 20, 2, rows4, palette4, 4)
    palette_image(rows4, palette4).save(data / "bmp_rle4_30x20.png")

    palette8 = [(i, (i * 3) % 256, (i * 5) % 256) for i in range(256)]
    rows8 = [[int(v) for v in rng.integers(0, 256, size=37)] for _ in range(23)]
    write_bmp_palette(data / "bmp_rle8_37x23.bmp", 37, 23, 1, rows8, palette8, 8)
    palette_image(rows8, palette8).save(data / "bmp_rle8_37x23.png")


def build_bmp_rle_delta(data: Path) -> None:
    palette = [(i, (i * 3) % 256, (i * 5) % 256) for i in range(256)]
    top = [(i * 9) % 256 for i in range(10)]
    bottom = [(i * 5 + 7) % 256 for i in range(10)]

    def absolute(pixels: list[int]) -> bytes:
        block = bytearray([0, len(pixels)])
        block.extend(pixels)
        if len(block) % 2:
            block.append(0)
        return bytes(block)

    stream = bytearray()
    stream += absolute([bottom[0], bottom[1], bottom[2], bottom[3]])
    stream += bytes([0, 2, 2, 0])
    stream += absolute([bottom[6], bottom[7], bottom[8], bottom[9]])
    stream += bytes([0, 0])
    if len(stream) % 2:
        stream += b"\x00"
    stream += absolute(top)
    stream += bytes([0, 0])
    if len(stream) % 2:
        stream += b"\x00"
    stream += bytes([0, 1])
    write_bmp_rle8_stream(data / "bmp_rle8_delta_12x2.bmp", 12, 2, palette, bytes(stream))
    expected = [list(top) + [0, 0],
                [bottom[0], bottom[1], bottom[2], bottom[3], 0, 0,
                 bottom[6], bottom[7], bottom[8], bottom[9], 0, 0]]
    palette_image(expected, palette).save(data / "bmp_rle8_delta_12x2.png")


def build_png_trns(data: Path) -> None:
    rgb = np.zeros((16, 16, 3), dtype=np.uint8)
    rgb[:, :, 0] = 200
    rgb[:, :, 1] = (np.arange(16) * 15)[None, :]
    rgb[:, :, 2] = 100
    rgb[4:8, 4:8] = (255, 0, 0)
    Image.fromarray(rgb, "RGB").save(data / "png_rgb_trns_16x16.png", transparency=(255, 0, 0))

    gray = np.full((16, 16), 120, dtype=np.uint8)
    gray[4:8, 4:8] = 33
    Image.fromarray(gray, "L").save(data / "png_gray_trns_16x16.png", transparency=33)


def build_corrupt(corrupt: Path) -> None:
    sources = ["rgba_33x27.png", "png_large.png", "rgb_64x48.pcx", "rgba_33x27.bmp",
               "tiff_deflate.tif", "jpeg_q95.jpg"]
    for name in sources:
        source = DATA / name
        if not source.exists():
            continue
        data = source.read_bytes()
        (corrupt / name).write_bytes(data[: len(data) * 2 // 3])

    palette = [(i, (i * 3) % 256, (i * 5) % 256) for i in range(256)]
    truncated = bytearray()
    truncated += bytes([0, 20]) + bytes(range(20))
    write_bmp_rle8_stream(corrupt / "bmp_rle8_truncated.bmp", 40, 30, palette, bytes(truncated))
    early = bytearray()
    early += bytes([0, 5]) + bytes(range(5))
    if len(early) % 2:
        early += b"\x00"
    early += bytes([0, 1])
    write_bmp_rle8_stream(corrupt / "bmp_rle8_early_eob.bmp", 40, 30, palette, bytes(early))

    (corrupt / "fake_image.png").write_bytes(b"this file has an image extension but is not one\n" * 8)
    (corrupt / "not_an_image.txt").write_bytes(b"this file is not an image at all\n" * 8)


def main() -> int:
    data = Path(sys.argv[1]) if len(sys.argv) > 1 else DATA
    corrupt = CORRUPT if data == DATA else data.parent / "corrupt"
    data.mkdir(parents=True, exist_ok=True)
    corrupt.mkdir(parents=True, exist_ok=True)
    build_bmp_rle(data)
    build_bmp_rle_delta(data)
    build_png_trns(data)
    build_corrupt(corrupt)
    print(f"edge cases written to {data}")
    print(f"corrupt samples written to {corrupt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())