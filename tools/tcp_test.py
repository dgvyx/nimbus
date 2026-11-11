#!/usr/bin/env python3
"""Teste TCP simples usando o desktop_smoke pattern"""
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
    "-netdev", "user,id=n0,hostfwd=tcp::8000-:8000", "-device", "rtl8139,netdev=n0,id=nic0",
], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

try:
    time.sleep(2)
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        f = sock.makefile("rwb", buffering=0)
        f.readline()

        def cmd(name, arguments=None):
            f.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
            while True:
                reply = json.loads(f.readline())
                if "error" in reply:
                    raise RuntimeError(reply)
                if "return" in reply:
                    return reply["return"]

        cmd("qmp_capabilities")

        def read_int(name, offset=0):
            path = args.output / "memory.bin"
            cmd("pmemsave", {"val": symbols[name] + offset, "size": 4, "filename": str(path.resolve())})
            return struct.unpack("<i", path.read_bytes())[0]

        def hmc(text):
            return cmd("human-monitor-command", {"command-line": text})

        def key(text):
            hmc("sendkey " + text)
            time.sleep(.12)

        def wait_until(cond, timeout=20):
            limit = time.monotonic() + timeout
            while time.monotonic() < limit:
                if cond():
                    return True
                time.sleep(.05)
            return False

        # Open terminal
        hmc("sendkey ctrl-spc")
        time.sleep(.3)
        for ch in "term": key(ch)
        key("ret")
        time.sleep(.5)

        # Run tcptest 10.0.2.2 8000
        for ch in "tcptest 10.0.2.2 8000":
            key(ch)
        key("ret")

        # Wait for connection to complete
        print("Waiting for TCP connection...")
        time.sleep(15)

        # Check terminal output via memory
        # Read tcp_conns state
        tcp_conns_addr = symbols["tcp_conns"]
        for i in range(4):
            conn_base = tcp_conns_addr + i * 88  # approximate TcpConn size
            state = read_int("tcp_conns", i * 88)
            print(f"Conn {i}: state={state}")

        # Take screenshot
        cmd("screendump", {"filename": str((args.output / "tcp_test.ppm").resolve()), "format": "ppm"})

        cmd("quit")
        f.close()
    p.wait(timeout=10)
finally:
    if p.poll() is None:
        p.terminate()
        p.wait(timeout=10)

print("Test completed")