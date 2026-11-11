#!/bin/bash
# Compila o Nimbus OS e gera nimbus.img (imagem de disco bootavel)
set -e
cd "$(dirname "$0")"
mkdir -p build
CF="-m32 -march=i686 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
    -fno-tree-loop-distribute-patterns -fcf-protection=none -fno-asynchronous-unwind-tables \
    -mgeneral-regs-only -O2 -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds"

as --32 -o build/stage1.o boot/stage1.s
ld -m elf_i386 -Ttext=0x7C00 --oformat=binary -o build/stage1.bin build/stage1.o
as --32 -o build/stage2.o boot/stage2.s
ld -m elf_i386 -Ttext=0x7E00 --oformat=binary -o build/stage2.bin build/stage2.o

as --32 -o build/entry.o kernel/entry.s
for f in gfx input ui net mem kernel html layout http; do gcc $CF -c kernel/$f.c -o build/$f.o; done
ld -m elf_i386 -T kernel/linker.ld -o build/kernel.elf \
    build/entry.o build/kernel.o build/gfx.o build/input.o build/ui.o build/net.o build/mem.o build/html.o build/layout.o build/http.o
objcopy -O binary build/kernel.elf build/kernel.bin
python3 tools/mkimage.py