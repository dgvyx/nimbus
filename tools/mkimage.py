#!/usr/bin/env python3
"""Monta nimbus.img: setor 0 = estagio 1, setores 1-8 = estagio 2, setor 9+ = kernel."""
import struct, subprocess, sys
b = lambda n: open(f"build/{n}", "rb").read()
s1, s2, k = b("stage1.bin"), b("stage2.bin"), b("kernel.bin")
assert len(s1) == 512 and s1[510:] == b"\x55\xaa", "estagio 1 deve ter 512 bytes"
assert len(s2) <= 4096, f"estagio 2 grande demais ({len(s2)})"
ksect = (len(k) + 511) // 512
bss_end = int(subprocess.check_output("nm build/kernel.elf | grep ' _bss_end'", shell=True).split()[0], 16)
assert bss_end < 0x80000, f"kernel+bss passa de 0x80000 (termina em {bss_end:#x})"
s2 = bytearray(s2.ljust(4096, b"\0"))
struct.pack_into("<I", s2, 4, ksect)
img = s1 + bytes(s2) + k.ljust(ksect * 512, b"\0")
img = img.ljust(max(len(img), 1024 * 1024), b"\0")      # minimo 1 MB
open("nimbus.img", "wb").write(img)
print(f"nimbus.img: {len(img)} bytes | estagio2 {len(s2.rstrip(b'\0'))} B | kernel {len(k)} B ({ksect} setores) | bss ate {bss_end:#x}")
