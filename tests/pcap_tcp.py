import struct
import sys

data = open(sys.argv[1], "rb").read()
offset = 24
n = 0
while offset + 16 <= len(data):
    _, _, incl, _ = struct.unpack("<IIII", data[offset:offset + 16])
    frame = data[offset + 16:offset + 16 + incl]
    offset += 16 + incl
    if len(frame) < 54 or frame[12:14] != b"\x08\x00":
        continue
    ip = frame[14:]
    ihl = (ip[0] & 15) * 4
    total = struct.unpack("!H", ip[2:4])[0]
    if ip[9] != 6:
        continue
    ip = ip[:total]
    tcp = ip[ihl:]
    sport, dport, seq, ack = struct.unpack("!HHII", tcp[:12])
    hdr = (tcp[12] >> 4) * 4
    flags = tcp[13]
    payload = tcp[hdr:]
    names = "".join(c for bit, c in ((1, "F"), (2, "S"), (4, "R"),
                                     (8, "P"), (16, "A")) if flags & bit)
    print(f"{n:2} {sport}->{dport} [{names}] seq={seq} ack={ack} "
          f"win={struct.unpack('!H', tcp[14:16])[0]} len={len(payload)} "
          f"{payload[:24]!r}")
    n += 1
