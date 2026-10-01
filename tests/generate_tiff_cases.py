"""Генератор проверочных TIFF-файлов в tests/data.

Запуск:  python3 tests/generate_tiff_cases.py [каталог]

Pillow умеет писать только LZW-вариант TIFF, поэтому PackBits, Deflate и
plain-варианты собираются вручную: заголовок и IFD пишутся напрямую, а
StripOffsets вычисляется после размещения всех значений.
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

TAG_SHORT = 3
TAG_LONG = 4
TAG_RATIONAL = 5
TYPE_SIZE = {TAG_SHORT: 2, TAG_LONG: 4, TAG_RATIONAL: 8}


def packbits(data: bytes) -> bytes:
    """Кодирует данные PackBits: литеральные серии не длиннее 128 байт."""
    out = bytearray()
    i = 0
    while i < len(data):
        length = min(128, len(data) - i)
        out.append(length - 1)
        out += data[i:i + length]
        i += length
    out.append(128)
    return bytes(out)


class TiffWriter:
    def __init__(self) -> None:
        self.values: dict[int, tuple[int, bytes]] = {}

    def set(self, tag: int, typ: int, *values: int) -> None:
        if typ == TAG_SHORT:
            payload = b"".join(struct.pack("<H", v) for v in values)
        elif typ == TAG_LONG:
            payload = b"".join(struct.pack("<I", v) for v in values)
        elif typ == TAG_RATIONAL:
            payload = b"".join(struct.pack("<II", n, d) for n, d in values)
        else:
            raise ValueError(f"неизвестный тип тега {typ}")
        self.values[tag] = (typ, payload)

    def build(self, width: int, height: int, compression: int, photometric: int,
              samples: int, strip: bytes, extra_samples: int | None) -> bytes:
        self.set(256, TAG_SHORT, width)
        self.set(257, TAG_SHORT, height)
        self.set(258, TAG_SHORT, *([8] * samples))
        self.set(259, TAG_SHORT, compression)
        self.set(262, TAG_SHORT, photometric)
        self.set(273, TAG_LONG, 0)
        self.set(277, TAG_SHORT, samples)
        self.set(278, TAG_SHORT, height)
        self.set(279, TAG_LONG, len(strip))
        self.set(284, TAG_SHORT, 1)
        if extra_samples is not None:
            self.set(338, TAG_SHORT, extra_samples)

        tags = sorted(self.values)
        ifd_size = 2 + len(tags) * 12 + 4
        overflow_start = 8 + ifd_size
        overflow = bytearray()
        placement: dict[int, int] = {}
        for tag in tags:
            _, payload = self.values[tag]
            if len(payload) > 4:
                placement[tag] = overflow_start + len(overflow)
                overflow += payload
                if len(overflow) % 2:
                    overflow += b"\x00"
        strip_offset = overflow_start + len(overflow)

        out = bytearray(struct.pack("<2sHI", b"II", 42, 8))
        out += struct.pack("<H", len(tags))
        for tag in tags:
            typ, payload = self.values[tag]
            count = len(payload) // TYPE_SIZE[typ]
            if tag == 273:
                field = struct.pack("<I", strip_offset)
            elif tag in placement:
                field = struct.pack("<I", placement[tag])
            else:
                field = payload.ljust(4, b"\x00")[:4]
            out += struct.pack("<HHI", tag, typ, count) + field
        out += struct.pack("<I", 0)
        out += overflow
        out += strip
        return bytes(out)


def write_tiff(path: Path, width: int, height: int, compression: int, strip: bytes,
               samples: int, photometric: int, extra_samples: int | None = None) -> None:
    writer = TiffWriter()
    path.write_bytes(writer.build(width, height, compression, photometric, samples, strip,
                                  extra_samples))


def write_tiff_lzw(path: Path, image: Image.Image) -> None:
    """Pillow формирует корректный LZW-поток TIFF, чем наш код не пользуется."""
    image.save(path, compression="tiff_lzw")


def build(target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(11)

    rgb = rng.integers(0, 256, size=(24, 31, 3), dtype=np.uint8)
    rgb_image = Image.fromarray(rgb, "RGB")
    rgb_image.save(target / "tiff_rgb_31x24.png")
    raw = rgb.tobytes()
    write_tiff(target / "tiff_rgb_packbits.tif", 31, 24, 32773, packbits(raw), 3, 2)
    write_tiff(target / "tiff_rgb_deflate.tif", 31, 24, 8, zlib.compress(raw), 3, 2)
    write_tiff_lzw(target / "tiff_rgb_lzw.tif", rgb_image)

    rgba = rng.integers(0, 256, size=(20, 17, 4), dtype=np.uint8)
    rgba_image = Image.fromarray(rgba, "RGBA")
    rgba_image.save(target / "tiff_rgba_17x20.png")
    write_tiff(target / "tiff_rgba_plain.tif", 17, 20, 1, rgba.tobytes(), 4, 2, extra_samples=2)
    write_tiff(target / "tiff_rgba_deflate.tif", 17, 20, 8, zlib.compress(rgba.tobytes()), 4, 2,
               extra_samples=2)
    write_tiff_lzw(target / "tiff_rgba_lzw.tif", rgba_image)

    gray = rng.integers(0, 256, size=(19, 23), dtype=np.uint8)
    gray_image = Image.fromarray(gray, "L")
    gray_image.save(target / "tiff_gray_23x19.png")
    write_tiff(target / "tiff_gray_packbits.tif", 23, 19, 32773, packbits(gray.tobytes()), 1, 1)
    write_tiff(target / "tiff_gray_deflate.tif", 23, 19, 8, zlib.compress(gray.tobytes()), 1, 1)
    write_tiff_lzw(target / "tiff_gray_lzw.tif", gray_image)

    print("tiff fixtures:", ", ".join(sorted(p.name for p in target.iterdir())))


def main() -> int:
    data = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent / "data"
    build(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())