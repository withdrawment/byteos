#!/usr/bin/env python3
"""Build a minimal ISO9660 + El Torito (no-emulation) bootable ISO.

The BIOS loads the entire boot image described by the El Torito catalog at
physical 0x7C00.  No GRUB, no floppy/hard-disk emulation, no external tools.
"""
from __future__ import annotations

import argparse
import struct
from datetime import datetime, timezone
from pathlib import Path

SECTOR = 2048


def le16(v: int) -> bytes:
    return struct.pack("<H", v)


def be16(v: int) -> bytes:
    return struct.pack(">H", v)


def le32(v: int) -> bytes:
    return struct.pack("<I", v)


def be32(v: int) -> bytes:
    return struct.pack(">I", v)


def both16(v: int) -> bytes:
    return le16(v) + be16(v)


def both32(v: int) -> bytes:
    return le32(v) + be32(v)


def pad(data: bytes, size: int, fill: bytes = b"\x00") -> bytes:
    if len(data) > size:
        raise ValueError("data does not fit")
    return data + fill * (size - len(data))


def dir_date(when: datetime) -> bytes:
    return bytes(
        [
            max(0, when.year - 1900) & 0xFF,
            when.month,
            when.day,
            when.hour,
            when.minute,
            when.second,
            0,  # UTC
        ]
    )


def volume_date(when: datetime) -> bytes:
    # 16 ASCII digits + timezone byte.
    return when.strftime("%Y%m%d%H%M%S00").encode("ascii") + b"\x00"


def dir_record(
    name: bytes,
    extent: int,
    size: int,
    flags: int,
    when: datetime,
) -> bytes:
    # ISO9660 9.1: 33 fixed bytes + identifier + optional pad.
    pad_len = 1 if len(name) % 2 == 0 else 0
    length = 33 + len(name) + pad_len
    rec = bytearray()
    rec.append(length)
    rec.append(0)  # extended attribute length
    rec += both32(extent)
    rec += both32(size)
    rec += dir_date(when)
    rec.append(flags)
    rec.append(0)  # file unit size
    rec.append(0)  # interleave gap
    rec += both16(1)  # volume sequence number
    rec.append(len(name))
    rec += name
    rec += b"\x00" * pad_len
    assert len(rec) == length
    return bytes(rec)


def build_iso(boot_image: bytes, readme: bytes, volume_id: str) -> bytes:
    boot_lba = 20
    boot_blocks = (len(boot_image) + SECTOR - 1) // SECTOR
    root_lba = boot_lba + boot_blocks
    path_l_lba = root_lba + 1
    path_m_lba = path_l_lba + 1
    readme_lba = path_m_lba + 1
    readme_blocks = (len(readme) + SECTOR - 1) // SECTOR
    total_sectors = readme_lba + readme_blocks

    now = datetime.now(timezone.utc)
    root_size = SECTOR

    # Root directory: ".", "..", README.TXT;1
    root = bytearray()
    root += dir_record(b"\x00", root_lba, root_size, 0x02, now)
    root += dir_record(b"\x01", root_lba, root_size, 0x02, now)
    root += dir_record(b"README.TXT;1", readme_lba, len(readme), 0x00, now)
    root = pad(bytes(root), SECTOR)

    # Primary Volume Descriptor (ECMA-119 8.4 field offsets).
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b" " * 32
    pvd[40:72] = pad(volume_id.upper().encode("ascii"), 32, b" ")
    pvd[72:80] = b"\x00" * 8
    pvd[80:88] = both32(total_sectors)
    pvd[88:120] = b"\x00" * 32
    pvd[120:124] = both16(1)   # volume set size
    pvd[124:128] = both16(1)   # volume sequence number
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(10)  # one 10-byte path table record
    pvd[140:144] = le32(path_l_lba)
    pvd[144:148] = le32(0)
    pvd[148:152] = be32(path_m_lba)
    pvd[152:156] = be32(0)
    pvd[156:190] = dir_record(b"\x00", root_lba, root_size, 0x02, now)
    pvd[190:318] = b" " * 128  # volume set identifier
    pvd[318:446] = b" " * 128  # publisher identifier
    pvd[446:574] = b" " * 128  # data preparer identifier
    pvd[574:702] = pad(b"BYTEOS ISO BUILDER", 128, b" ")
    pvd[702:739] = b" " * 37   # copyright file identifier
    pvd[739:776] = b" " * 37   # abstract file identifier
    pvd[776:813] = b" " * 37   # bibliographic file identifier
    pvd[813:830] = volume_date(now)  # creation
    pvd[830:847] = volume_date(now)  # modification
    pvd[847:864] = b"0" * 16 + b"\x00"  # expiration (unset)
    pvd[864:881] = b"0" * 16 + b"\x00"  # effective (unset)
    pvd[881] = 1               # file structure version
    pvd[882] = 0

    # Volume Descriptor Set Terminator.
    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    # El Torito Boot Record Volume Descriptor.
    boot_record = bytearray(SECTOR)
    boot_record[0] = 0
    boot_record[1:6] = b"CD001"
    boot_record[6] = 1
    boot_record[7:39] = pad(b"EL TORITO SPECIFICATION", 32, b"\x00")
    boot_record[71:75] = le32(19)  # boot catalog LBA

    # Boot catalog: validation entry + default no-emulation entry.
    catalog = bytearray(SECTOR)
    catalog[0] = 0x01  # validation entry
    catalog[1] = 0x00  # 80x86
    catalog[2:4] = b"\x00\x00"
    catalog[4:28] = pad(b"BYTEOS BOOT CATALOG", 24)
    catalog[28:30] = b"\x00\x00"  # checksum filled below
    catalog[30] = 0x55
    catalog[31] = 0xAA
    words_sum = sum(struct.unpack("<16H", bytes(catalog[:32])))
    checksum = (-words_sum) & 0xFFFF
    catalog[28:30] = le16(checksum)

    entry = bytearray(32)
    entry[0] = 0x88  # bootable
    entry[1] = 0x00  # no emulation
    entry[2:4] = le16(0x07C0)  # load at physical 0x7C00
    entry[4] = 0  # system type
    entry[6:8] = le16((len(boot_image) + 511) // 512)
    entry[8:12] = le32(boot_lba)
    catalog[32:64] = entry

    # Path tables (single root record).
    path_l = bytearray()
    path_l.append(1)          # identifier length
    path_l.append(0)          # extended attribute length
    path_l += le32(root_lba)
    path_l += le16(1)         # parent directory number
    path_l += b"\x00"         # root identifier
    path_l += b"\x00"         # even-length padding
    path_l_table = pad(bytes(path_l), SECTOR)

    path_m = bytearray()
    path_m.append(1)
    path_m.append(0)
    path_m += be32(root_lba)
    path_m += be16(1)
    path_m += b"\x00"
    path_m += b"\x00"
    path_m_table = pad(bytes(path_m), SECTOR)

    image = bytearray(total_sectors * SECTOR)
    image[16 * SECTOR:17 * SECTOR] = pvd
    image[17 * SECTOR:18 * SECTOR] = boot_record
    image[18 * SECTOR:19 * SECTOR] = term
    image[19 * SECTOR:20 * SECTOR] = catalog
    image[boot_lba * SECTOR:(boot_lba + boot_blocks) * SECTOR] = pad(
        boot_image, boot_blocks * SECTOR
    )
    image[root_lba * SECTOR:(root_lba + 1) * SECTOR] = root
    image[path_l_lba * SECTOR:(path_l_lba + 1) * SECTOR] = path_l_table
    image[path_m_lba * SECTOR:(path_m_lba + 1) * SECTOR] = path_m_table
    if readme_blocks:
        image[readme_lba * SECTOR:(readme_lba + readme_blocks) * SECTOR] = pad(
            readme, readme_blocks * SECTOR
        )

    return bytes(image)


def apply_mbr(iso: bytes, mbr: bytes) -> bytes:
    image = bytearray(iso)
    if len(mbr) > 512:
        raise ValueError("MBR image must be at most 512 bytes")
    code = bytearray(mbr[:512].ljust(512, b"\x00"))
    entry = bytearray(16)
    entry[0] = 0x80
    entry[1:4] = b"\xfe\xff\xff"
    entry[4] = 0x83
    entry[5:8] = b"\xfe\xff\xff"
    entry[8:12] = le32(0)
    entry[12:16] = le32(len(image) // 512)
    code[446:462] = entry
    code[462:510] = b"\x00" * 48
    code[510:512] = b"\x55\xaa"
    image[0:512] = code
    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("boot_image")
    parser.add_argument("output")
    parser.add_argument("--readme", default=None)
    parser.add_argument("--volume-id", default="BYTEOS")
    parser.add_argument("--mbr", default=None)
    args = parser.parse_args()

    boot_image = Path(args.boot_image).read_bytes()
    if not boot_image:
        raise SystemExit("boot image is empty")
    if len(boot_image) > 0x9F000:
        raise SystemExit("boot image is too large for the BIOS load area")

    readme = b"ByteOS 64-bit kernel\n"
    if args.readme:
        readme = Path(args.readme).read_bytes()

    iso = build_iso(boot_image, readme, args.volume_id)
    if args.mbr:
        if len(iso) < 2 * 1024 * 1024:
            iso += b"\x00" * (2 * 1024 * 1024 - len(iso))
        image = bytearray(iso)
        image[16 * SECTOR + 80:16 * SECTOR + 88] = both32(
            len(image) // SECTOR
        )
        iso = apply_mbr(bytes(image), Path(args.mbr).read_bytes())
    Path(args.output).write_bytes(iso)
    print(
        "wrote %s (%d bytes, boot image %d bytes / %d sectors)"
        % (args.output, len(iso), len(boot_image),
           (len(boot_image) + 511) // 512)
    )


if __name__ == "__main__":
    main()
