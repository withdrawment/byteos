#!/usr/bin/env python3
"""Drive the ByteOS installer in a running QEMU session and screenshot it."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455

KEYS = {
    " ": "spc", "-": "minus", ".": "dot", "_": "shift-minus",
    "/": "slash", "\\": "backslash",
}


def connect() -> socket.socket:
    for _ in range(80):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.settimeout(3)
            return s
        except OSError:
            time.sleep(0.25)
    raise SystemExit("could not connect to QEMU monitor")


def drain(s: socket.socket, quiet: float = 0.25) -> str:
    s.settimeout(quiet)
    out = b""
    while True:
        try:
            chunk = s.recv(65536)
            if not chunk:
                break
            out += chunk
        except socket.timeout:
            break
        except ConnectionResetError:
            break
    return out.decode("latin-1", errors="replace")


def cmd(s: socket.socket, command: str, wait: float = 0.3) -> str:
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    return drain(s)


def type_text(s: socket.socket, text: str) -> None:
    for ch in text:
        key = KEYS.get(ch, ch.lower())
        cmd(s, "sendkey " + key, 0.06)


def enter(s: socket.socket, wait: float = 0.5) -> None:
    cmd(s, "sendkey ret", wait)


def line(s: socket.socket, text: str, wait: float = 0.5) -> None:
    type_text(s, text)
    enter(s, wait)


def shot(s: socket.socket, path: Path) -> None:
    cmd(s, "screendump %s" % path.resolve(), 0.8)
    print("captured", path.name)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/install")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(2.5)
    shot(s, out / "live_home.ppm")

    line(s, "fetch", 0.5)
    time.sleep(0.5)
    shot(s, out / "fetch.ppm")
    cmd(s, "sendkey spc", 0.4)

    line(s, "byte-install", 1.2)
    shot(s, out / "installer_disks.ppm")

    line(s, "1", 0.6)
    line(s, "byte", 0.5)
    line(s, "byte", 0.5)
    line(s, "byte", 0.5)
    line(s, "root", 0.5)
    line(s, "root", 0.5)
    enter(s, 0.5)           # accept hostname byteos
    line(s, "install", 0.5)
    print("installing...")
    for i in range(12):
        time.sleep(1.0)
        try:
            cmd(s, "screendump %s" % (out / ("progress_%02d.ppm" % i)).resolve(), 0.5)
            print("captured progress_%02d.ppm" % i)
        except ConnectionResetError:
            print("QEMU reset during install at step", i)
            return
    shot(s, out / "installer_done.ppm")

    cmd(s, "sendkey r", 1.0)
    try:
        cmd(s, "quit", 0.2)
    except ConnectionResetError:
        pass
    s.close()


if __name__ == "__main__":
    main()
