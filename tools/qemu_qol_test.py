#!/usr/bin/env python3
"""Check ByteWin 95 double-click maximize, help layout and restart."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455
KEYS = {" ": "spc", "-": "minus"}


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


def move(s: socket.socket, dx: int, dy: int, wait: float = 0.4) -> None:
    cmd(s, "mouse_move %d %d" % (dx, dy), wait)


def click(s: socket.socket, wait: float = 0.4) -> None:
    s.sendall(b"mouse_button 1\n")
    time.sleep(0.05)
    drain(s, 0.03)
    s.sendall(b"mouse_button 0\n")
    time.sleep(wait)
    drain(s, 0.03)


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
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/qol")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(2.0)
    run(s, "start-desktop win95", 2.5)

    key(s, "f10", 0.6)
    for _ in range(8):
        key(s, "down", 0.1)
    key(s, "ret", 1.5)
    move(s, 270, -204)
    move(s, -290, 0)
    click(s, 0.1)
    click(s)
    shot(s, out / "01-dbl-max.ppm")

    time.sleep(0.6)
    move(s, 0, -25)
    click(s, 0.1)
    click(s)
    shot(s, out / "02-dbl-restore.ppm")

    key(s, "f10", 0.6)
    for _ in range(2):
        key(s, "down", 0.15)
    key(s, "ret", 0.8)
    run(s, "help", 0.9)
    shot(s, out / "03-help.ppm")

    key(s, "f10", 0.6)
    for _ in range(11):
        key(s, "down", 0.1)
    shot(s, out / "04-restart-item.ppm")
    key(s, "ret", 5.0)
    shot(s, out / "05-reboot.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
