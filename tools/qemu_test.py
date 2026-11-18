#!/usr/bin/env python3
"""Drive a running QEMU HMP monitor: screenshot, type, screenshot again."""
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


def drain(s: socket.socket, quiet: float = 0.3) -> str:
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


def cmd(s: socket.socket, command: str, wait: float = 0.4) -> str:
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    return drain(s)


def main() -> None:
    out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "build")
    out_dir.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)

    # Give the kernel time to draw its home screen.
    time.sleep(2.5)
    cmd(s, "screendump %s" % (out_dir / "boot.ppm").resolve(), 1.0)
    print("captured boot screen")

    for key in ("d", "o", "n", "u", "t"):
        cmd(s, "sendkey " + key, 0.12)
    cmd(s, "sendkey ret", 0.8)

    # Let a few donut frames render.
    time.sleep(2.0)
    cmd(s, "screendump %s" % (out_dir / "donut.ppm").resolve(), 1.0)
    print("captured donut screen")

    # Any key should leave the donut and restore the home screen.
    cmd(s, "sendkey spc", 0.5)
    time.sleep(1.0)
    cmd(s, "screendump %s" % (out_dir / "back.ppm").resolve(), 1.0)
    print("captured post-donut screen")

    cmd(s, "quit", 0.5)
    s.close()


if __name__ == "__main__":
    main()
