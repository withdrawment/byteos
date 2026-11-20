#!/usr/bin/env python3
"""Exercise ByteTile: tiling, launcher, tags, layouts, config, tileconf."""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 4455

KEYS = {
    " ": "spc", "-": "minus", ".": "dot", "_": "shift-minus",
    "/": "slash", "\\": "backslash", "=": "equal",
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


def combo(s: socket.socket, *names: str, wait: float = 0.35) -> None:
    key(s, "-".join(names), wait)


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
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/tile")
    out.mkdir(parents=True, exist_ok=True)
    s = connect()
    drain(s, 1.0)
    time.sleep(3.0)
    shot(s, out / "00-boot.ppm")

    run(s, "start-desktop", 1.0)
    shot(s, out / "01-chooser.ppm")
    key(s, "3", 0.6)
    shot(s, out / "02-tile.ppm")

    combo(s, "alt", "ret", wait=1.0)
    shot(s, out / "03-two-terms.ppm")

    run(s, "ls", 0.8)
    shot(s, out / "04-term-ls.ppm")

    combo(s, "alt", "2", wait=0.6)
    shot(s, out / "05-tag2-welcome.ppm")
    combo(s, "alt", "1", wait=0.5)

    combo(s, "alt", "p", wait=0.6)
    shot(s, out / "06-launcher.ppm")
    key(s, "esc", 0.4)

    combo(s, "meta_l", "spc", wait=0.6)
    shot(s, out / "06b-super-launcher.ppm")
    key(s, "esc", 0.4)

    combo(s, "meta_l", "ret", wait=1.0)
    shot(s, out / "06c-super-term.ppm")
    combo(s, "meta_l", "q", wait=0.7)
    shot(s, out / "06d-super-close.ppm")

    combo(s, "meta_l", "spc", wait=0.6)
    key(s, "down", 0.25)
    key(s, "ret", 1.2)
    shot(s, out / "06e-browser.ppm")
    key(s, "2", 1.2)
    shot(s, out / "06f-browser-docs.ppm")
    key(s, "u", 0.4)
    run(s, "www/manual.html", 1.2)
    shot(s, out / "06g-browser-manual.ppm")
    key(s, "esc", 0.8)

    run(s, "clear", 0.4)
    run(s, "help", 0.9)
    shot(s, out / "06h-cmd-help.ppm")
    run(s, "clear", 0.4)
    run(s, "specs", 1.0)
    shot(s, out / "06i-cmd-specs.ppm")
    run(s, "clear", 0.4)
    run(s, "grep ByteTile www/index.html", 1.0)
    shot(s, out / "06j-cmd-grep.ppm")

    combo(s, "alt", "spc", wait=0.5)
    shot(s, out / "07-monocle.ppm")
    combo(s, "alt", "spc", wait=0.5)
    shot(s, out / "08-grid.ppm")

    combo(s, "alt", "shift", "c", wait=0.8)
    shot(s, out / "09-config.ppm")

    combo(s, "ctrl", "s", wait=0.7)
    shot(s, out / "10-config-saved.ppm")

    combo(s, "ctrl", "r", wait=0.7)
    shot(s, out / "11-config-reloaded.ppm")

    key(s, "f12", 2.0)
    shot(s, out / "12-shell.ppm")

    run(s, "tileconf", 1.0)
    shot(s, out / "13-tileconf.ppm")

    cmd(s, "quit", 0.3)
    s.close()


if __name__ == "__main__":
    main()
