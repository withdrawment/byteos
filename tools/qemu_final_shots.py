#!/usr/bin/env python3
"""Capture the final ByteOS 0.2 screenshot set from an installed disk."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455

KEYS = {" ": "spc", ".": "dot", "-": "minus", "_": "shift-minus",
        "/": "slash", "\\": "backslash"}


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


def key(s: socket.socket, name: str, wait: float = 0.5) -> None:
    cmd(s, "sendkey " + name, wait)


def type_text(s: socket.socket, text: str) -> None:
    for ch in text:
        cmd(s, "sendkey " + KEYS.get(ch, ch.lower()), 0.06)


def run(s: socket.socket, text: str, wait: float = 0.8) -> None:
    type_text(s, text)
    key(s, "ret", wait)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/final")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(3.0)
    shot(s, out / "login.ppm")
    run(s, "byte", 0.4)
    run(s, "byte", 1.5)
    shot(s, out / "home.ppm")

    run(s, "fetch", 1.0)
    shot(s, out / "fetch.ppm")
    key(s, "spc", 0.5)

    run(s, "specs", 1.0)
    shot(s, out / "specs.ppm")

    run(s, "about", 1.0)
    shot(s, out / "about.ppm")

    run(s, "help", 1.0)
    shot(s, out / "help.ppm")

    run(s, "start-desktop", 1.2)
    shot(s, out / "chooser.ppm")
    key(s, "1", 0.4)
    key(s, "ret", 1.5)
    shot(s, out / "desktop.ppm")

    key(s, "b", 1.2)
    shot(s, out / "browser.ppm")
    key(s, "esc", 0.8)

    key(s, "f", 1.2)
    shot(s, out / "files.ppm")
    key(s, "esc", 0.8)

    key(s, "e", 1.2)
    shot(s, out / "editor.ppm")
    key(s, "esc", 0.8)

    key(s, "t", 1.2)
    run(s, "fetch", 1.2)
    shot(s, out / "terminal.ppm")
    key(s, "spc", 0.8)
    shot(s, out / "terminal_after.ppm")
    run(s, "exit", 1.0)

    for _ in range(2):
        key(s, "down", 0.3)
    key(s, "ret", 1.2)          # Donut
    time.sleep(1.0)
    shot(s, out / "donut.ppm")
    key(s, "spc", 0.8)
    key(s, "f10", 1.0)
    shot(s, out / "console.ppm")

    run(s, "start-desktop", 1.2)
    key(s, "2", 0.4)
    key(s, "ret", 2.5)
    shot(s, out / "win95.ppm")

    key(s, "f10", 0.6)
    shot(s, out / "win95-menu.ppm")
    for _ in range(8):
        key(s, "down", 0.15)
    key(s, "ret", 1.5)
    shot(s, out / "win95-fetch.ppm")
    key(s, "esc", 0.8)

    key(s, "f10", 0.6)
    key(s, "ret", 1.2)
    shot(s, out / "win95-files.ppm")
    key(s, "esc", 0.6)

    key(s, "f10", 0.6)
    key(s, "down", 0.3)
    key(s, "down", 0.3)
    key(s, "ret", 1.2)
    run(s, "help", 0.8)
    run(s, "pwd", 0.6)
    run(s, "history", 0.8)
    shot(s, out / "win95-console.ppm")
    key(s, "f12", 1.8)

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
