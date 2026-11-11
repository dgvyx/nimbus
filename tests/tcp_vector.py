import struct

# Independent reference: sum words using Python integers, then end-around carry.
def checksum(data):
    data += b'\0' * (len(data) % 2)
    total = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while total > 0xffff:
        total = (total & 0xffff) + (total >> 16)
    return total ^ 0xffff

syn = bytes.fromhex('c0001f4012345678000000006002ffff00000000020405b4')
for segment in (syn, syn + b'X'):
    pseudo = bytes.fromhex('0a00020f0a0002020006') + struct.pack('!H', len(segment))
    print(f'{len(segment)} bytes: 0x{checksum(pseudo + segment):04x}')
