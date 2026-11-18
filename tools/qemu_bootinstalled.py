#!/usr/bin/env python3
"""Boot an installed ByteOS disk image, log in, and capture screens."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455


def connect() -> socket.socket:
    for _ in range(80):
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


def cmd(s: socket.socket, command: str, wait: float = 0.3) -> None:
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    drain(s)


KEYS = {" ": "spc", ".": "dot", "-": "minus", "_": "shift-minus",
        "/": "slash", "\\": "backslash"}


def type_text(s: socket.socket, text: str) -> None:
    for ch in text.lower():
        cmd(s, "sendkey " + KEYS.get(ch, ch), 0.07)


def run(s: socket.socket, text: str, wait: float = 0.5) -> None:
    type_text(s, text)
    cmd(s, "sendkey ret", wait)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/installed")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(3.0)
    shot(s, out / "login.ppm")

    run(s, "byte", 0.4)
    run(s, "byte", 1.2)
    shot(s, out / "home.ppm")

    run(s, "fetch", 0.8)
    shot(s, out / "fetch.ppm")
    cmd(s, "sendkey spc", 0.5)

    run(s, "ls", 0.7)
    shot(s, out / "ls.ppm")

    run(s, "cat home/notes.html", 0.8)
    shot(s, out / "cat.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
