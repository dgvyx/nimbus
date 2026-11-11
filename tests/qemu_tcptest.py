#!/usr/bin/env python3
"""Boot the real nimbus.img in QEMU, run `tcptest 10.0.2.2 8000` from the
Terminal and verify the HTTP response through the guest terminal buffer.

QEMU user networking maps 10.0.2.2 to the host loopback, so a local HTTP
server on 127.0.0.1:8000 is reachable. The terminal text buffer (tbuf) is
read with QMP pmemsave, so no OCR is needed.
"""
import argparse
import functools
import http.server
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "nimbus.img"
PCAP = ROOT / "tests" / "tcptest.pcap"
TBUF_SIZE = 48 * 100

# QEMU sendkey names for the punctuation used in terminal commands/URLs.
KEY_NAMES = {
    ".": "dot", "/": "slash", ":": "shift-semicolon", "-": "minus",
    "_": "shift-minus", "?": "shift-slash", "=": "equal", "+": "shift-equal",
    "&": "shift-7", "#": "shift-3", "%": "shift-5", ",": "comma",
    ";": "semicolon", "'": "apostrophe", "@": "shift-2", "~": "shift-grave",
}


def find_tool(name, env, fallback=None):
    path = os.environ.get(env) or shutil.which(name)
    if path:
        return path
    if fallback and Path(fallback).exists():
        return fallback
    sys.exit(f"{name} not found; set {env} or install it in PATH")


def symbols(elf, nm):
    out = {}
    for line in subprocess.check_output([nm, str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            out[fields[2]] = int(fields[0], 16)
    return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/r0":
            self.redirect("http://10.0.2.2:8000/r1")
            return
        if self.path == "/r1":
            self.redirect("http://10.0.2.2:8000/r2")
            return
        if self.path == "/r2":
            self.redirect("http://10.0.2.2:8000/")
            return
        if self.path == "/large":
            body = b"0123456789abcdef" * 512  # 8192 bytes, no newlines
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/redirect":
            self.redirect("http://10.0.2.2:8000/")
            return
        if self.path == "/redirect-https":
            self.send_response(301)
            self.send_header("Location", "https://10.0.2.2:8000/")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        super().do_GET()

    def redirect(self, location):
        self.send_response(302)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()


def serve(directory):
    handler = functools.partial(Quiet, directory=str(directory))
    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 8000), handler)
    except OSError:
        print("port 8000 already in use; using the server already running there")
        return None
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def rst_server(port):
    """Accept a connection and abort it with RST (SO_LINGER 0)."""
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(8)

    def loop():
        while True:
            try:
                client, _ = listener.accept()
            except OSError:
                return
            client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                              struct.pack("ii", 1, 0))
            client.close()

    threading.Thread(target=loop, daemon=True).start()
    return listener


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--command", default="tcptest 10.0.2.2 8000")
    parser.add_argument("--expect", default="HTTP/1.0 200 OK")
    parser.add_argument("--pcap", type=Path, default=PCAP)
    parser.add_argument("--image", type=Path, default=IMAGE)
    parser.add_argument("--kernel", type=Path, default=ROOT / "build" / "kernel.elf")
    parser.add_argument("--qemu", default=None,
                        help="qemu-system-i386 path (default: $QEMU or PATH)")
    parser.add_argument("--nm", default=None,
                        help="nm path (default: $NM or PATH)")
    parser.add_argument("--rst-port", type=int, default=0,
                        help="also listen here and abort connections with RST")
    args = parser.parse_args()

    qemu_path = args.qemu or find_tool(
        "qemu-system-i386", "QEMU", r"C:\Program Files\qemu\qemu-system-i386.exe")
    nm_path = args.nm or find_tool("nm", "NM")
    assert args.image.exists(), f"missing {args.image}; build it first"
    assert args.kernel.exists(), f"missing {args.kernel}; build it first"
    web = ROOT / "tests" / "qemu" / "www"
    server = serve(web)
    rst = rst_server(args.rst_port) if args.rst_port else None
    syms = symbols(args.kernel, nm_path)

    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]

    args.pcap.unlink(missing_ok=True)
    qemu = subprocess.Popen([
        qemu_path, "-drive", f"file={args.image},format=raw", "-snapshot",
        "-m", "128", "-display", "none",
        "-qmp", f"tcp:127.0.0.1:{port},server,nowait",
        "-netdev", "user,id=n0", "-device", "rtl8139,netdev=n0,id=nic0",
        "-object", f"filter-dump,id=f0,netdev=n0,file={args.pcap}",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    ok = False
    try:
        time.sleep(3)
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            stream = sock.makefile("rwb", buffering=0)
            stream.readline()

            def command(name, arguments=None):
                stream.write((json.dumps({"execute": name,
                                          "arguments": arguments or {}}) + "\n").encode())
                while True:
                    reply = json.loads(stream.readline())
                    if "error" in reply:
                        raise RuntimeError(reply)
                    if "return" in reply:
                        return reply["return"]

            command("qmp_capabilities")

            def monitor(text):
                return command("human-monitor-command", {"command-line": text})

            def key(name, pause=0.15):
                monitor("sendkey " + name)
                time.sleep(pause)

            command("screendump", {"filename": str((ROOT / "tests" / "tcptest_boot.ppm").resolve()),
                                   "format": "ppm"})
            key("ctrl-spc", 0.35)
            for ch in "term":
                key(ch)
            key("ret", 0.7)

            for ch in args.command:
                if ch == " ":
                    name = "spc"
                elif ch in KEY_NAMES:
                    name = KEY_NAMES[ch]
                elif ch.isupper():
                    name = "shift-" + ch.lower()
                else:
                    name = ch
                key(name)
            key("ret", 0.2)

            deadline = time.monotonic() + 20
            dump = ROOT / "tests" / "tbuf.bin"
            text = ""
            while time.monotonic() < deadline:
                command("pmemsave", {"val": syms["tbuf"],
                                     "size": TBUF_SIZE, "filename": str(dump.resolve())})
                raw = dump.read_bytes()
                rows = [raw[i:i + 100].split(b"\0")[0].decode("latin-1")
                        for i in range(0, TBUF_SIZE, 100)]
                text = "\n".join(r for r in rows if r)
                if args.expect in text:
                    ok = True
                    break
                time.sleep(0.5)

            command("screendump", {"filename": str((ROOT / "tests" / "tcptest_terminal.ppm").resolve()),
                                   "format": "ppm"})
            command("quit")
            stream.close()
            print(text)

        qemu.wait(timeout=10)
    except Exception:
        if qemu.poll() is None:
            qemu.terminate()
            qemu.wait(timeout=10)
        raise
    finally:
        if qemu.poll() is None:
            qemu.terminate()
        if server is not None:
            server.shutdown()
        if rst is not None:
            rst.close()

    if not ok:
        print(f"\nRESULT FAIL: expected '{args.expect}' in tbuf", file=sys.stderr)
        return 1
    if args.expect == "HTTP/1.0 200 OK" and (
            not args.pcap.exists() or args.pcap.stat().st_size < 200):
        print("RESULT FAIL: no packet capture produced", file=sys.stderr)
        return 1
    print("RESULT PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
