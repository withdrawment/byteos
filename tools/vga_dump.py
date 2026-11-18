#!/usr/bin/env python3
"""Decode a raw 80x25 VGA text-memory dump into plain text."""
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_bytes()
for row in range(25):
    line = []
    for col in range(80):
        off = (row * 80 + col) * 2
        if off + 1 >= len(raw):
            break
        ch = raw[off]
        line.append(chr(ch) if 32 <= ch < 127 else " ")
    print("".join(line).rstrip())
