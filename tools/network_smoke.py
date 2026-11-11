#!/usr/bin/env python3
"""Exercita o driver real no QEMU NAT: ARP, ping, DNS, ring e link."""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

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
    "-netdev", "user,id=n0", "-device", "rtl8139,netdev=n0,id=nic0",
    "-object", f"filter-dump,id=capture,netdev=n0,file={args.output.resolve() / 'network.pcap'}",
], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
try:
    time.sleep(2)
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        f = sock.makefile("rwb", buffering=0)
        f.readline()

        def cmd(name, arguments=None):
            f.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
            while True:
                r = json.loads(f.readline())
                if "error" in r:
                    raise RuntimeError(r)
                if "return" in r:
                    return r["return"]

        cmd("qmp_capabilities")

        def state():
            path = args.output / "net.bin"
            cmd("pmemsave", {"val": symbols["net"], "size": 152, "filename": str(path.resolve())})
            data = path.read_bytes()
            values = struct.unpack_from("<14I", data)
            return {"present": values[0], "link": values[1], "busy": values[2],
                    "resolved": values[8], "tx": values[9], "rx": values[10],
                    "dropped": values[11], "ping_ms": values[12],
                    "status": data[56:].split(b"\0")[0].decode("ascii", errors="replace")}

        def monitor(text):
            return cmd("human-monitor-command", {"command-line": text})

        def key(text):
            monitor("sendkey " + text)
            time.sleep(.3)

        position = [640, 360]

        def move(x, y):
            while position != [x, y]:
                dx = max(-100, min(100, x - position[0]))
                dy = max(-100, min(100, y - position[1]))
                monitor(f"mouse_move {dx} {dy}")
                position[0] += dx; position[1] += dy
                time.sleep(.04)
            time.sleep(.15)

        def click():
            monitor("mouse_button 1"); time.sleep(.06)
            monitor("mouse_button 0"); time.sleep(.06)

        def wait_result(before_tx, timeout=7):
            limit = time.monotonic() + timeout
            while time.monotonic() < limit:
                info = state()
                if info["tx"] > before_tx and not info["busy"]:
                    return info
                time.sleep(.03)
            raise AssertionError(f"Operacao sem resposta: {state()}")

        assert state()["present"] == 1, state()
        assert state()["link"] == 1, state()
        key("ctrl-spc")
        for ch in "rede": key(ch)
        key("ret")
        move(810, 254)
        before = state()["tx"]
        click()
        info = wait_result(before)
        assert info["status"] == "Gateway respondeu ao ping", info
        print("PING:", info)
        # Cada transacao inclui ARP e ICMP: ultrapassa o ring de 8 KiB.
        for i in range(72):
            before = state()["tx"]
            click()
            info = wait_result(before)
            assert info["status"] == "Gateway respondeu ao ping", (i, info)
        assert info["rx"] >= 146 and info["dropped"] == 0, info
        print("RX ring wrap: OK", info["rx"], "pacotes")
        before = state()["tx"]
        key("ret")
        info = wait_result(before)
        assert info["resolved"] != 0, info
        print("DNS example.com:", socket.inet_ntoa(struct.pack("!I", info["resolved"])))
        time.sleep(.25)
        cmd("screendump", {"filename": str((args.output / "network.png").resolve()), "format": "png"})
        cmd("set_link", {"name": "nic0", "up": False})
        time.sleep(1.2)
        assert state()["link"] == 0, state()
        click()
        assert state()["busy"] == 0, state()
        cmd("set_link", {"name": "nic0", "up": True})
        time.sleep(1.2)
        before = state()["tx"]
        click()
        assert wait_result(before)["status"] == "Gateway respondeu ao ping"
        print("OK: RTL8139, ARP, ICMP, DNS externo, ring wrap, link down/up.")
        cmd("quit")
        f.close()
    p.wait(timeout=10)
finally:
    if p.poll() is None:
        p.terminate(); p.wait(timeout=10)
