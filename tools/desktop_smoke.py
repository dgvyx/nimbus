#!/usr/bin/env python3
"""Teste integrado em QEMU isolado: eventos, overlays e residuos de desenho.

Requer Pillow, imagem/ELF compilados e nm para i686-elf.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

from PIL import Image, ImageChops

parser = argparse.ArgumentParser()
parser.add_argument("--qemu", default="qemu-system-i386")
parser.add_argument("--nm", default="nm")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
symbols = {}
for line in subprocess.check_output([args.nm, "build/kernel.elf"], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)

with socket.socket() as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
p = subprocess.Popen([
    args.qemu, "-drive", "file=nimbus.img,format=raw", "-snapshot", "-m", "128",
    "-display", "none", "-qmp", f"tcp:127.0.0.1:{port},server,nowait",
], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
try:
    time.sleep(2)
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        stream = sock.makefile("rwb", buffering=0)
        stream.readline()

        def command(name, arguments=None):
            stream.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
            while True:
                reply = json.loads(stream.readline())
                if "error" in reply:
                    raise RuntimeError(reply)
                if "return" in reply:
                    return reply["return"]

        command("qmp_capabilities")

        def read_int(name, offset=0):
            path = args.output / "memory.bin"
            command("pmemsave", {"val": symbols[name] + offset, "size": 4, "filename": str(path.resolve())})
            return struct.unpack("<i", path.read_bytes())[0]

        def monitor(text):
            return command("human-monitor-command", {"command-line": text})

        def key(text):
            monitor("sendkey " + text)
            time.sleep(.3)

        position = [640, 360]

        def move(x, y):
            while position != [x, y]:
                dx = max(-100, min(100, x - position[0]))
                dy = max(-100, min(100, y - position[1]))
                monitor(f"mouse_move {dx} {dy}")
                position[0] += dx
                position[1] += dy
                time.sleep(.04)
            time.sleep(.4)

        def button(value):
            monitor(f"mouse_button {value}")
            time.sleep(.12)

        def click(x, y):
            move(x, y)
            button(1)
            button(0)

        def screenshot(name):
            path = args.output / (name + ".png")
            command("screendump", {"filename": str(path.resolve()), "format": "png"})
            with Image.open(path) as image:
                return image.convert("RGB")

        def same_desktop(a, b):
            return ImageChops.difference(a.crop((0, 30, 1280, 720)), b.crop((0, 30, 1280, 720))).getbbox() is None

        start = screenshot("01-desktop")
        assert start.size == (1280, 720)
        ticks = read_int("timer_ticks")
        move(640, 680)
        hover = screenshot("02-dock")
        assert not same_desktop(start, hover)
        move(640, 360)
        assert same_desktop(start, screenshot("03-dock-restaurado")), "Residuos do dock/cursor"
        assert read_int("timer_ticks") > ticks, "Timer parado"

        move(700, 127)
        button(1)
        move(780, 187)
        screenshot("04-arraste")
        move(700, 127)
        button(0)
        move(640, 360)
        assert same_desktop(start, screenshot("05-arraste-restaurado")), "Residuos do arraste"

        click(24, 15)
        assert read_int("menu") == 0
        screenshot("06-menu")
        click(90, 85)
        assert read_int("search_open") == 1
        for ch in "calc":
            key(ch)
        assert read_int("search_len") == 4
        screenshot("07-busca")
        key("ret")
        assert read_int("search_open") == 0
        assert read_int("wins", 2 * 48 + 16) == 1, "Calculadora nao abriu"
        for ch in ["2", "shift-equal", "3", "ret"]:
            key(ch)
        assert read_int("ccur") == 5
        screenshot("08-calculadora")
        key("alt-f4")
        assert read_int("wins", 2 * 48 + 16) == 0
        key("ctrl-m")
        assert read_int("wins", 48 + 20) == 1, "Notas nao minimizou"
        key("ctrl-spc")
        for ch in "notas":
            key(ch)
        key("ret")
        assert read_int("wins", 48 + 20) == 0, "Notas nao restaurou"
        key("ctrl-spc")
        key("down")
        assert read_int("search_selected") == 1
        key("up")
        assert read_int("search_selected") == 0
        key("ret")
        assert read_int("wins", 16) == 1, "Terminal nao abriu"
        key("alt-tab")
        assert read_int("zorder", (read_int("zn") - 1) * 4) == 1
        key("ctrl-spc")
        for ch in "zzz":
            key(ch)
        key("ret")
        assert read_int("search_open") == 1, "Busca vazia abriu um app"
        key("esc")
        screenshot("09-busca-fechada")
        assert read_int("search_open") == 0
        print("OK: boot, timer, dock, arraste sem residuos, menus, busca, calculadora e atalhos.")
        key("ctrl-spc")
        for ch in "ajustes": key(ch)
        key("ret")
        assert read_int("wins", 4 * 48 + 16) == 1
        click(614, 203)
        assert read_int("appearance_palette") == 1
        click(506, 307)
        assert read_int("accent") == 0x218A71
        click(782, 352)
        assert read_int("dock_animation") == 0
        screenshot("10-ajustes")
        key("ctrl-spc")
        for ch in "notas": key(ch)
        key("ret")
        move(700, 127)
        button(1)
        before_frames, before_ticks = read_int("rendered_frames"), read_int("render_ticks")
        started = time.monotonic()
        for i in range(120):
            dx = 5 if (i // 30) % 2 == 0 else -5
            monitor(f"mouse_move {dx} 0")
            position[0] += dx
            time.sleep(1 / 60)
        elapsed = time.monotonic() - started
        frames = read_int("rendered_frames") - before_frames
        ticks = read_int("render_ticks") - before_ticks
        assert read_int("drag_cached") == 1 and frames > 0
        screenshot("11-cache-arraste")
        button(0)
        assert read_int("drag") == -1
        print(f"Arraste: {frames / elapsed:.1f} quadros/s; desenho medio {ticks * 1000 / 240 / frames:.1f} ms (QEMU TCG).")
        print("OK: Ajustes, papel de parede, cor de destaque, animacao e cache de arraste.")
        command("quit")
        stream.close()
    p.wait(timeout=10)
finally:
    if p.poll() is None:
        p.terminate()
        p.wait(timeout=10)
