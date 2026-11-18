#!/usr/bin/env python3
"""Drive the ByteWin 95 desktop through the QEMU monitor for smoke tests."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455
KEYS = {" ": "spc", ".": "dot", "-": "minus", "_": "shift-minus",
        "/": "slash", "\\": "backslash", "=": "equal",
        "'": "apostrophe", '"': "shift-apostrophe"}


def connect() -> socket.socket:
    for _ in range(80):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.settimeout(3)
            return s
        except OSError:
            time.sleep(0.25)
    raise SystemExit("could not connect to QEMU monitor")


def drain(s: socket.socket, quiet: float = 0.2) -> None:
    s.settimeout(quiet)
    while True:
        try:
            if not s.recv(65536):
                return
        except (socket.timeout, ConnectionResetError):
            return


def cmd(s: socket.socket, command: str, wait: float = 0.25) -> None:
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    drain(s)


def key(s: socket.socket, name: str, wait: float = 0.3) -> None:
    cmd(s, "sendkey " + name, wait)


def type_text(s: socket.socket, text: str) -> None:
    for ch in text:
        if ch.isupper():
            cmd(s, "sendkey shift-" + ch.lower(), 0.05)
        else:
            cmd(s, "sendkey " + KEYS.get(ch, ch), 0.05)


def run(s: socket.socket, text: str, wait: float = 0.7) -> None:
    type_text(s, text)
    key(s, "ret", wait)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name, flush=True)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/guitest")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(2.0)

    run(s, "start-desktop win95", 2.5)
    shot(s, out / "01-win95.ppm")

    key(s, "f10", 0.6)
    shot(s, out / "02-menu.ppm")
    for _ in range(9):
        key(s, "down", 0.12)
    shot(s, out / "03-fetch-item.ppm")
    key(s, "ret", 1.5)
    shot(s, out / "04-fetch.ppm")

    key(s, "esc", 0.8)
    shot(s, out / "05-fetch-closed.ppm")

    key(s, "f10", 0.6)
    for _ in range(10):
        key(s, "down", 0.12)
    key(s, "ret", 1.8)
    shot(s, out / "06-fetch-tui.ppm")
    key(s, "spc", 1.2)
    shot(s, out / "07-back.ppm")

    key(s, "f10", 0.6)
    for _ in range(2):
        key(s, "down", 0.15)
    key(s, "ret", 0.8)
    run(s, "help", 0.8)
    run(s, "pwd", 0.6)
    run(s, "history", 0.8)
    shot(s, out / "08-history.ppm")
    key(s, "up", 0.3)
    key(s, "up", 0.3)
    shot(s, out / "09-history-recall.ppm")

    key(s, "f12", 2.0)
    shot(s, out / "10-after-exit.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
