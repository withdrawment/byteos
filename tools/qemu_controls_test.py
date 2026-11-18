#!/usr/bin/env python3
"""Exercise ByteWin 95 window controls through the QEMU monitor."""
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


KEYS = {" ": "spc", "-": "minus"}


def type_text(s: socket.socket, text: str) -> None:
    for ch in text:
        if ch.isupper():
            cmd(s, "sendkey shift-" + ch.lower(), 0.05)
        else:
            cmd(s, "sendkey " + KEYS.get(ch, ch), 0.05)


def run(s: socket.socket, text: str, wait: float = 0.7) -> None:
    type_text(s, text)
    key(s, "ret", wait)


def move(s: socket.socket, dx: int, dy: int, wait: float = 0.5) -> None:
    cmd(s, "mouse_move %d %d" % (dx, dy), wait)


def click(s: socket.socket, wait: float = 0.45) -> None:
    s.sendall(b"mouse_button 1\n")
    time.sleep(0.05)
    drain(s, 0.03)
    s.sendall(b"mouse_button 0\n")
    time.sleep(wait)
    drain(s, 0.03)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name, flush=True)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/controls")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(2.0)
    move(s, -270, 204)
    run(s, "start-desktop win95", 2.5)

    key(s, "f10", 0.6)
    for _ in range(8):
        key(s, "down", 0.1)
    key(s, "ret", 1.5)
    shot(s, out / "01-fetch-native.ppm")

    move(s, 270, -204)
    shot(s, out / "02-cursor-max.ppm")
    click(s)
    shot(s, out / "03-maximized.ppm")

    move(s, 21, -25)
    click(s)
    shot(s, out / "04-restored.ppm")

    move(s, -38, 30)
    click(s)
    shot(s, out / "05-minimized.ppm")

    move(s, -473, 424)
    click(s)
    shot(s, out / "06-taskbar-restore.ppm")

    time.sleep(0.8)
    move(s, 200, -430)
    click(s, 0.08)
    click(s)
    shot(s, out / "07-doubleclick-max.ppm")

    time.sleep(0.8)
    move(s, 0, -25)
    click(s, 0.08)
    click(s)
    shot(s, out / "08-doubleclick-restore.ppm")

    move(s, 309, 27)
    shot(s, out / "09-before-close.ppm")
    click(s)
    shot(s, out / "10-closed.ppm")

    key(s, "f10", 0.6)
    for _ in range(2):
        key(s, "down", 0.15)
    key(s, "ret", 0.8)
    for ch in "fetch":
        key(s, ch, 0.08)
    key(s, "ret", 1.5)
    shot(s, out / "11-fetch-command.ppm")

    key(s, "f12", 2.0)
    shot(s, out / "12-shell.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
