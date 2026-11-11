# Nimbus OS - entrada do kernel (modo protegido 32 bits, em 0x10000)
.section .text.entry, "ax"
.code32
.global _start
_start:
    cli
    cld
    lgdt gdtr
    ljmp $0x08, $1f
1:  movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp
    movl $_bss_start, %edi          # zera o BSS
    movl $_bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    movl $1, %eax
    cpuid
    testl $(1 << 26), %edx         # SSE2 opcional para copiar pixels
    jz 4f
    movl %cr0, %eax
    andl $~4, %eax
    orl $2, %eax
    movl %eax, %cr0
    movl %cr4, %eax
    orl $0x600, %eax
    movl %eax, %cr4
    movl $1, gfx_sse2
4:
    call kmain
2:  hlt
    jmp 2b

# Qualquer excecao da CPU cai aqui: a tela fica vermelha em vez de reiniciar.
.global timer_stub
timer_stub:
    pushl %eax
    incl timer_ticks
    movb $0x20, %al
    outb %al, $0x20
    popl %eax
    iret

.global isr_stub
isr_stub:
    cli
    call fault_handler
3:  hlt
    jmp 3b

.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdtr:
    .word 23
    .long gdt

# Apenas estas rotinas usam XMM; o C continua -mgeneral-regs-only.
# IRQ0 preserva EAX e nao usa XMM. ESI/EDI sao preservados (ABI i386).
.global memcpy32_sse
memcpy32_sse:
    pushl %edi
    pushl %esi
    movl 12(%esp), %edi
    movl 16(%esp), %esi
    movl 20(%esp), %ecx
1:  cmpl $16, %ecx
    jb 2f
    movdqu 0(%esi), %xmm0
    movdqu 16(%esi), %xmm1
    movdqu 32(%esi), %xmm2
    movdqu 48(%esi), %xmm3
    movdqu %xmm0, 0(%edi)
    movdqu %xmm1, 16(%edi)
    movdqu %xmm2, 32(%edi)
    movdqu %xmm3, 48(%edi)
    addl $64, %esi
    addl $64, %edi
    subl $16, %ecx
    jmp 1b
2:  rep movsl
    popl %esi
    popl %edi
    ret

.global memset32_sse
memset32_sse:
    pushl %edi
    movl 8(%esp), %edi
    movl 12(%esp), %eax
    movl 16(%esp), %ecx
    movd %eax, %xmm0
    pshufd $0, %xmm0, %xmm0
1:  cmpl $16, %ecx
    jb 2f
    movdqu %xmm0, 0(%edi)
    movdqu %xmm0, 16(%edi)
    movdqu %xmm0, 32(%edi)
    movdqu %xmm0, 48(%edi)
    addl $64, %edi
    subl $16, %ecx
    jmp 1b
2:  rep stosl
    popl %edi
    ret
