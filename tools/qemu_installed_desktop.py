#!/usr/bin/env python3
"""Exercise ByteDesktop and editor saving on an installed ByteOS disk."""
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
    for ch in text.lower():
        cmd(s, "sendkey " + KEYS.get(ch, ch), 0.06)


def run(s: socket.socket, text: str, wait: float = 0.7) -> None:
    type_text(s, text)
    key(s, "ret", wait)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/installed_desktop")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(3.0)
    shot(s, out / "login.ppm")
    run(s, "byte", 0.4)
    run(s, "byte", 1.5)
    shot(s, out / "home.ppm")

    run(s, "start-desktop", 1.0)
    key(s, "b", 1.5)
    shot(s, out / "browser.ppm")

    key(s, "2", 1.5)
    shot(s, out / "browser_docs.ppm")
    key(s, "3", 1.5)
    shot(s, out / "browser_install.ppm")

    key(s, "esc", 0.8)
    key(s, "f", 1.2)
    shot(s, out / "files.ppm")

    for _ in range(6):
        key(s, "down", 0.15)
    key(s, "f4", 1.2)
    shot(s, out / "editor.ppm")

    type_text(s, "hello ")
    cmd(s, "sendkey ctrl-s", 1.0)
    shot(s, out / "editor_saved.ppm")

    key(s, "esc", 0.8)
    shot(s, out / "files_after.ppm")
    key(s, "esc", 0.8)
    key(s, "f10", 0.8)
    shot(s, out / "console.ppm")

    run(s, "cat home/notes.html", 1.0)
    shot(s, out / "cat_notes.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
