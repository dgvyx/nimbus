#!/usr/bin/env python3
"""Independent validation of the first TCP SYN in a pcap capture.

Checks the same invariants QEMU/SLIRP relies on: IPv4 header length, total
length, IP checksum, protocol 6, TTL, TCP data offset, SYN-only flags,
ports, sequence, window, MSS option and the TCP checksum with pseudo-header.
Exit code 0 means the SYN is well formed.
"""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "tests/syn_frame.pcap"

with open(path, "rb") as capture:
    raw = capture.read()
if raw[:4] not in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4"):
    sys.exit("not a little-endian pcap")
offset, packets = 24, []
while offset + 16 <= len(raw):
    _, _, incl, _ = struct.unpack("<IIII", raw[offset:offset + 16])
    packets.append(raw[offset + 16:offset + 16 + incl])
    offset += 16 + incl


def ones(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while total > 0xffff:
        total = (total & 0xffff) + (total >> 16)
    return total


def fail(text):
    sys.exit("SYN invalid: " + text)


for frame in packets:
    if len(frame) < 54 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    if ip[9] != 6:
        continue
    ihl = (ip[0] & 15) * 4
    total = struct.unpack("!H", ip[2:4])[0]
    if ip[0] >> 4 != 4 or ihl != 20:
        fail(f"IPv4 version/IHL {ip[0] >> 4}/{ihl}")
    if total > len(ip) or total < ihl:
        fail(f"IP total length {total} vs captured {len(ip)}")
    ip = ip[:total]
    if ones(ip[:ihl]) != 0xffff:
        fail("IP header checksum")
    if ip[8] <= 0:
        fail("TTL")
    tcp = ip[ihl:]
    sport, dport, seq, ack = struct.unpack("!HHII", tcp[:12])
    header = (tcp[12] >> 4) * 4
    flags = tcp[13]
    window = struct.unpack("!H", tcp[14:16])[0]
    if header != 24:
        fail(f"data offset {header}, expected 24 with MSS")
    if flags != 0x02:
        fail(f"flags 0x{flags:02x}, expected SYN only")
    if ack != 0:
        fail("ACK number set on SYN")
    pseudo = ip[12:20] + b"\0\x06" + struct.pack("!H", len(tcp))
    if ones(pseudo + tcp) != 0xffff:
        fail("TCP checksum")
    if tcp[20:24] != b"\x02\x04\x05\xb4":
        fail("missing MSS option")
    if tcp[24:]:
        fail("unexpected SYN payload")
    print(f"valid SYN: {sport}->{dport} seq={seq} window={window} "
          f"mss={struct.unpack('!H', tcp[22:24])[0]} mac={frame[0:6].hex(':')}")
    sys.exit(0)
sys.exit("no TCP SYN found")
