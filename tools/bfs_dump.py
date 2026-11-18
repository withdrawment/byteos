#!/usr/bin/env python3
"""Read ByteFS files out of a raw ByteOS disk image."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

PART_LBA = 2048
SECTOR = 512
NAME_LEN = 40
MAX_FILES = 32
FILE_STRUCT = 52


def main() -> None:
    image = Path(sys.argv[1] if len(sys.argv) > 1 else "build/byteos-disk.img")
    outdir = Path(sys.argv[2] if len(sys.argv) > 2 else "build/bfs_dump")
    outdir.mkdir(parents=True, exist_ok=True)
    raw = image.read_bytes()
    base = PART_LBA * SECTOR
    sb = raw[base:base + 4 * SECTOR]
    magic = sb[0:8]
    (version, max_files, part_lba, data_lba, next_lba, end_lba) = \
        struct.unpack_from("<6I", sb, 8)
    print("magic", magic, "version", version, "max", max_files,
          "part", part_lba, "data", data_lba, "next", next_lba,
          "end", end_lba)
    for i in range(MAX_FILES):
        off = 48 + i * FILE_STRUCT
        name = sb[off:off + NAME_LEN].split(b"\x00")[0].decode("latin-1")
        start, size = struct.unpack_from("<II", sb, off + NAME_LEN)
        flags = sb[off + NAME_LEN + 8]
        if not (flags & 1):
            continue
        data = raw[start * SECTOR:start * SECTOR + size]
        print("%2d %-24s %6d bytes flags=%02x" % (i, name, size, flags))
        dest = outdir / name.replace("/", "_")
        dest.write_bytes(data)


if __name__ == "__main__":
    main()
