#!/usr/bin/env python3
"""Install ByteOS to a disk, then edit and verify /bytetile.conf persistence."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455
KEYS = {" ": "spc", "-": "minus", ".": "dot", "_": "shift-minus",
        "/": "slash", "\\": "backslash", "=": "equal"}


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


def key(s: socket.socket, name: str, wait: float = 0.3) -> None:
    cmd(s, "sendkey " + name, wait)


def type_text(s: socket.socket, text: str) -> None:
    for ch in text:
        if ch.isupper():
            cmd(s, "sendkey shift-" + ch.lower(), 0.06)
        else:
            cmd(s, "sendkey " + KEYS.get(ch, ch), 0.06)


def line(s: socket.socket, text: str, wait: float = 0.5) -> None:
    type_text(s, text)
    key(s, "ret", wait)


def combo(s: socket.socket, *names: str, wait: float = 0.35) -> None:
    key(s, "-".join(names), wait)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name, flush=True)


def install(s: socket.socket, out: Path, disk: str = "1") -> None:
    cmd(s, "sendkey ret", 0.5)
    shot(s, out / "install-00-live.ppm")
    line(s, "byte-install", 1.2)
    shot(s, out / "install-01-disks.ppm")
    line(s, disk, 0.6)
    line(s, "byte", 0.5)
    line(s, "byte", 0.5)
    line(s, "byte", 0.5)
    line(s, "root", 0.5)
    line(s, "root", 0.5)
    key(s, "ret", 0.5)
    line(s, "install", 0.6)
    for i in range(20):
        time.sleep(1.0)
        shot(s, out / ("install-progress-%02d.ppm" % i))
    shot(s, out / "install-02-done.ppm")


def login(s: socket.socket, out: Path) -> None:
    time.sleep(3.0)
    shot(s, out / "boot-login.ppm")
    line(s, "byte", 0.5)
    line(s, "byte", 1.2)
    shot(s, out / "boot-home.ppm")


def edit(s: socket.socket, out: Path) -> None:
    line(s, "start-desktop", 1.0)
    key(s, "3", 1.2)
    shot(s, out / "edit-00-tile.ppm")

    combo(s, "alt", "shift", "c", wait=0.9)
    shot(s, out / "edit-01-config.ppm")

    for _ in range(14):
        key(s, "down", 0.06)
    key(s, "end", 0.1)
    key(s, "backspace", 0.1)
    key(s, "1", 0.08)
    key(s, "2", 0.1)
    shot(s, out / "edit-02-typed.ppm")

    combo(s, "ctrl", "s", wait=0.8)
    shot(s, out / "edit-03-saved.ppm")

    key(s, "f12", 2.0)
    shot(s, out / "edit-04-shell.ppm")
    line(s, "tileconf", 1.0)
    shot(s, out / "edit-05-tileconf.ppm")


def verify(s: socket.socket, out: Path) -> None:
    line(s, "tileconf", 1.0)
    shot(s, out / "verify-00-tileconf.ppm")


def main() -> None:
    stage = sys.argv[1] if len(sys.argv) > 1 else "install"
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "build/tile-install")
    disk = sys.argv[3] if len(sys.argv) > 3 else "1"
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    if stage == "install":
        install(s, out, disk)
    elif stage == "edit":
        login(s, out)
        edit(s, out)
    elif stage == "verify":
        login(s, out)
        verify(s, out)
    else:
        raise SystemExit("unknown stage: " + stage)
    cmd(s, "quit", 0.2)
    s.close()


if __name__ == "__main__":
    main()
