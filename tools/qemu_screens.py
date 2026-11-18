#!/usr/bin/env python3
"""Type ByteOS commands into QEMU and capture each screen."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455


def connect() -> socket.socket:
    for _ in range(60):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.settimeout(3)
            return s
        except OSError:
            time.sleep(0.25)
    raise SystemExit("could not connect to QEMU monitor")


def drain(s: socket.socket, quiet: float = 0.25) -> None:
    s.settimeout(quiet)
    while True:
        try:
            if not s.recv(65536):
                return
        except (socket.timeout, ConnectionResetError):
            return


def cmd(s: socket.socket, command: str, wait: float = 0.35) -> None:
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    drain(s)


def type_text(s: socket.socket, text: str) -> None:
    for ch in text:
        if ch.isalnum():
            key = ch.lower()
        elif ch == " ":
            key = "spc"
        elif ch == "-":
            key = "minus"
        elif ch == ".":
            key = "dot"
        else:
            raise SystemExit("no key mapping for %r" % ch)
        cmd(s, "sendkey " + key, 0.08)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name)


def run(s: socket.socket, out: Path, command: str, name: str) -> None:
    type_text(s, command)
    cmd(s, "sendkey ret", 0.7)
    time.sleep(0.6)
    shot(s, out / (name + ".ppm"))


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build")
    out.mkdir(parents=True, exist_ok=True)

    s = connect()
    drain(s, 1.0)
    time.sleep(2.5)
    shot(s, out / "home.ppm")

    run(s, out, "specs", "specs")
    cmd(s, "pmemsave 0xb8000 4000 %s" % (out / "specs.bin").resolve(), 0.5)
    run(s, out, "help", "help")
    run(s, out, "memmap", "memmap")
    cmd(s, "pmemsave 0xb8000 4000 %s" % (out / "memmap.bin").resolve(), 0.5)
    run(s, out, "about", "about")

    # Donut, twice, to prove that it animates.
    type_text(s, "donut")
    cmd(s, "sendkey ret", 0.8)
    time.sleep(1.0)
    shot(s, out / "donut_a.ppm")
    time.sleep(1.0)
    shot(s, out / "donut_b.ppm")
    cmd(s, "sendkey spc", 0.5)
    time.sleep(0.8)
    shot(s, out / "after_donut.ppm")

    try:
        cmd(s, "quit", 0.3)
    except ConnectionResetError:
        pass
    s.close()


if __name__ == "__main__":
    main()
