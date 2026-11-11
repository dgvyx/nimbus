#!/usr/bin/env python3
"""Validate every TCP frame in a pcap (IP/TCP checksums, offsets, padding)."""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "tests/rede.pcap"
data = open(path, "rb").read()
offset = 24


def ones(chunk):
    if len(chunk) % 2:
        chunk += b"\0"
    total = sum(struct.unpack("!" + "H" * (len(chunk) // 2), chunk))
    while total > 0xffff:
        total = (total & 0xffff) + (total >> 16)
    return total


seen = syn = synack = 0
while offset + 16 <= len(data):
    _, _, incl, _ = struct.unpack("<IIII", data[offset:offset + 16])
    frame = data[offset + 16:offset + 16 + incl]
    offset += 16 + incl
    if len(frame) < 34 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    if ip[9] != 6:
        continue
    ihl = (ip[0] & 15) * 4
    total = struct.unpack("!H", ip[2:4])[0]
    if ihl < 20 or total < ihl + 20 or total > len(ip):
        sys.exit(f"frame {seen}: bad lengths ihl={ihl} total={total}")
    segment = ip[ihl:total]
    header = (segment[12] >> 4) * 4
    if header < 20 or header > len(segment):
        sys.exit(f"frame {seen}: bad data offset {header}")
    if ones(ip[:ihl]) != 0xffff:
        sys.exit(f"frame {seen}: bad IP checksum")
    pseudo = ip[12:20] + b"\0\x06" + struct.pack("!H", len(segment))
    if ones(pseudo + segment) != 0xffff:
        sys.exit(f"frame {seen}: bad TCP checksum")
    sport, dport = struct.unpack("!HH", segment[:4])
    flags = segment[13]
    seen += 1
    if flags == 0x02:
        syn += 1
    if flags == 0x12:
        synack += 1
    print(f"OK frame {seen}: {sport}->{dport} flags=0x{flags:02x} "
          f"len={len(segment)} (padded to {len(frame)})")

if not seen:
    sys.exit("no TCP frames")
if syn == 0 or synack == 0:
    sys.exit(f"incomplete handshake: SYN={syn} SYN-ACK={synack}")
print(f"ALL {seen} TCP FRAMES VALID (SYN={syn} SYN-ACK={synack})")
