# Nimbus OS - estagio 2 (carregado em 0x7E00)
# 1) liga A20  2) carrega o kernel em 0x10000  3) escolhe e ativa modo de video
#    VBE de 32 bits com framebuffer linear  4) entra em modo protegido  5) pula
#    para o kernel. Informacoes de video ficam em 0x600 (struct bootinfo).
.code16
.intel_syntax noprefix
.section .text
.global _start
_start:
    jmp stage2_main
    .org 4
kernel_sectors: .long 0         # preenchido por tools/mkimage.py

stage2_main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    mov ax, 0x2401              # A20 via BIOS
    int 0x15
    in al, 0x92                 # A20 via porta rapida
    or al, 2
    and al, 0xFE
    out 0x92, al

    call load_kernel
    call set_video

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    .byte 0x66, 0xEA            # jmp far 0x08:pm_entry (32 bits)
    .long pm_entry
    .word 0x08

# ---------------------------------------------------------------- kernel
# Kernel esta no disco a partir do LBA 9. Vai para 0x1000:0000 (0x10000).
load_kernel:
    mov eax, [kernel_sectors]
    mov [remaining], eax
1:  mov eax, [remaining]
    test eax, eax
    jz 3f
    cmp eax, 64
    jbe 2f
    mov eax, 64
2:  mov [dap_cnt], ax
    mov [chunk], ax
    mov si, offset dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error
    movzx eax, word ptr [chunk]
    sub [remaining], eax
    add [dap_lba], eax
    shl ax, 5                   # setores*512/16 paragrafos
    add [dap_seg], ax
    jmp 1b
3:  ret

# ----------------------------------------------------------------- video
set_video:
    mov dword ptr [0x9000], 0x32454256      # "VBE2"
    mov di, 0x9000
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne video_error

    mov si, [0x900E]            # copia a lista de modos para 0x9400
    mov ax, [0x9010]
    push ds
    mov ds, ax
    mov di, 0x9400
    mov cx, 250
1:  lodsw
    stosw
    cmp ax, 0xFFFF
    je 2f
    loop 1b
    mov ax, 0xFFFF
    stosw
2:  pop ds

    mov bp, offset want_list
want_next:
    mov ax, [bp]
    test ax, ax
    jz video_error
    mov [want_w], ax
    mov ax, [bp+2]
    mov [want_h], ax
    add bp, 4
    mov si, 0x9400
scan:
    mov cx, [si]
    cmp cx, 0xFFFF
    je want_next
    add si, 2
    mov [cur_mode], cx
    push si
    push bp
    mov di, 0x9200
    mov ax, 0x4F01
    int 0x10
    pop bp
    pop si
    cmp ax, 0x004F
    jne scan
    mov ax, [0x9200]
    and ax, 0x0091              # suportado + grafico + framebuffer linear
    cmp ax, 0x0091
    jne scan
    cmp byte ptr [0x9200+25], 32
    jne scan
    cmp byte ptr [0x9200+27], 6 # cor direta
    jne scan
    mov ax, [0x9200+18]
    cmp ax, [want_w]
    jne scan
    mov ax, [0x9200+20]
    cmp ax, [want_h]
    jne scan

    mov bx, [cur_mode]
    or bx, 0x4000               # usar framebuffer linear
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne video_error

    mov dword ptr [0x600], 0x4E494D42       # magica "BMIN"
    mov eax, [0x9200+40]
    mov [0x604], eax                        # endereco do framebuffer
    movzx eax, word ptr [0x9200+16]
    mov [0x608], eax                        # bytes por linha (padrao)
    movzx eax, word ptr [0x9200+18]
    mov [0x60C], eax
    movzx eax, word ptr [0x9200+20]
    mov [0x610], eax
    mov dword ptr [0x614], 32
    mov ax, 0x4F06                          # confirma bytes por linha reais
    mov bl, 1
    int 0x10
    cmp ax, 0x004F
    jne 1f
    movzx ebx, bx
    mov [0x608], ebx
1:  ret

video_error:
    mov si, offset msg_video
    jmp print_hang
disk_error:
    mov si, offset msg_disk
print_hang:
1:  lodsb
    test al, al
    jz 2f
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp 1b
2:  hlt
    jmp 2b

msg_video: .asciz "Nimbus: sem modo de video 32 bits"
msg_disk:  .asciz "Nimbus: erro ao ler o kernel"

# resolucoes desejadas, em ordem de preferencia (0 termina)
want_list: .word 1280,720, 1024,768, 800,600, 0,0

boot_drive: .byte 0
.align 4
remaining: .long 0
chunk:     .word 0
want_w:    .word 0
want_h:    .word 0
cur_mode:  .word 0

.align 4
dap:
    .byte 0x10, 0
dap_cnt: .word 0
dap_off: .word 0
dap_seg: .word 0x1000
dap_lba: .long 9, 0

.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF    # codigo 32 bits, base 0, limite 4 GB
    .quad 0x00CF92000000FFFF    # dados
gdt_end:
gdt_desc:
    .word gdt_end - gdt - 1
    .long gdt

.code32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov eax, 0x10000
    jmp eax
