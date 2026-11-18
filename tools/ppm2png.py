#!/usr/bin/env python3
"""Convert a binary PPM (P6) screenshot to PNG using only the stdlib."""
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path: Path):
    data = path.read_bytes()
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    if tokens[0] != b"P6":
        raise SystemExit("not a binary PPM")
    width, height, maxval = map(int, tokens[1:4])
    if maxval != 255:
        raise SystemExit("unsupported maxval")
    while pos < len(data) and data[pos:pos + 1].isspace():
        pos += 1
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise SystemExit("truncated PPM")
    return width, height, pixels


def chunk(tag: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_png(path: Path, width: int, height: int, pixels: bytes):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: ppm2png.py input.ppm output.png")
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    width, height, pixels = read_ppm(src)
    write_png(dst, width, height, pixels)
    print("wrote %s (%dx%d)" % (dst, width, height))


if __name__ == "__main__":
    main()
