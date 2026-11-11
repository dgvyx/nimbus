# Nimbus OS - estagio 1 (setor de boot, 512 bytes)
# A BIOS carrega este setor em 0x7C00. Ele le o estagio 2 (8 setores, LBA 1)
# para 0x7E00 usando a extensao LBA da int 13h (funciona em pendrive).
.code16
.intel_syntax noprefix
.section .text
.global _start
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov si, offset dap
    mov ah, 0x42
    int 0x13
    jc disk_error
    mov dl, [boot_drive]
    .byte 0xEA                  # jmp far 0x0000:0x7E00
    .word 0x7E00, 0x0000

disk_error:
    mov si, offset msg_err
1:  lodsb
    test al, al
    jz 2f
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp 1b
2:  hlt
    jmp 2b

msg_err: .asciz "Nimbus: erro de disco"

.align 4
dap:
    .byte 0x10, 0
    .word 8                     # setores a ler
    .word 0x7E00, 0x0000        # destino offset:segmento
    .quad 1                     # LBA inicial
boot_drive: .byte 0

# Tabela de particoes "de enfeite": algumas BIOS de pendrive so bootam se houver
# uma particao ativa. Marca LBA 1..2047 como ativa (tipo 0xDA = dados brutos).
.org 446
    .byte 0x80, 0x00, 0x02, 0x00, 0xDA, 0xFE, 0xFF, 0xFF
    .long 1, 2047

.org 510
.word 0xAA55
