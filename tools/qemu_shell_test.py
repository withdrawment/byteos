#!/usr/bin/env python3
"""Type shell commands into a running QEMU monitor and capture screens."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455
KEYS = {" ": "spc", ".": "dot", "-": "minus", "_": "shift-minus",
        "/": "slash", "\\": "backslash", "=": "equal", "'": "apostrophe",
        '"': "shift-apostrophe", "$": "shift-4", "~": "shift-grave"}


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


def key(s: socket.socket, name: str, wait: float = 0.35) -> None:
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
    cmd(s, "screendump %s" % path.resolve(), 0.7)
    print("captured", path.name)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/shelltest")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(2.5)

    run(s, "help", 0.8)
    shot(s, out / "help.ppm")
    run(s, "lsblk", 0.8)
    shot(s, out / "lsblk.ppm")
    run(s, "df", 0.7)
    run(s, "free", 0.7)
    run(s, "uname -a", 0.7)
    run(s, "date", 0.7)
    run(s, "uptime", 0.7)
    run(s, "hostname", 0.6)
    run(s, "echo hello $USER in $PWD", 0.7)
    shot(s, out / "info.ppm")
    run(s, "pwd", 0.5)
    run(s, "start-desktop", 0.8)
    shot(s, out / "chooser.ppm")
    key(s, "esc", 0.5)
    shot(s, out / "after_cancel.ppm")
    run(s, "start-desktop win95", 2.0)
    shot(s, out / "win95.ppm")
    key(s, "f10", 0.6)
    shot(s, out / "win95-menu.ppm")
    key(s, "f12", 1.8)
    shot(s, out / "after_win95.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
