/* Multiboot harness: runs kernel/net.c against QEMU user-mode networking.
 * Built only for tests/; it is not part of nimbus.img. */
#include "nimbus.h"
#include "net.h"

#define TEST_IP(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(d))

volatile u32 timer_ticks;

__asm__(
".section .multiboot,\"a\"\n"
".align 4\n"
".long 0x1BADB002\n"
".long 0x00000003\n"
".long -(0x1BADB002 + 3)\n"
".text\n"
".global _start\n"
"_start:\n"
"movl $0x300000, %esp\n"
"xorl %ebp, %ebp\n"
"call kmain\n"
"1: hlt\n"
"jmp 1b\n"
);

static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void put_char(char c) {
    while (!(inb(0x3F8 + 5) & 0x20)) {}
    outb(0x3F8, (u8)c);
}

static void put_text(const char *s) {
    while (s && *s) put_char(*s++);
}

static void put_num(u32 v) {
    char digits[12];
    int n = 0;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) put_char(digits[--n]);
}

static void qemu_exit(int ok) {
    outl(0x501, ok ? 0x10 : 0x11);
    for (;;) __asm__ volatile("hlt");
}

/* The host test prints response bytes, so it needs a few libc symbols. */
void *memcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, u32 n) {
    volatile u8 *d = dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *memmove(void *dst, const void *src, u32 n) {
    volatile u8 *d = dst; const u8 *s = src;
    if (d > s) { while (n) { n--; d[n] = s[n]; } }
    else memcpy(dst, src, n);
    return dst;
}

static u8 heap[320 * 1024] __attribute__((aligned(16)));
static u32 heap_used;

void *kmalloc(u32 size) {
    u32 at = (heap_used + 15) & ~15u;
    if (size > sizeof(heap) - at) return 0;
    heap_used = at + size;
    return heap + at;
}

void kfree(void *pointer) { (void)pointer; }

void kmain(void) {
    serial_init();
    put_text("nimbus tcp qemu test\n");
    net_init();
    put_text(net.status);
    put_text("\n");
    if (!net.present || !net.link) {
        put_text("RESULT FAIL no nic\n");
        qemu_exit(0);
    }

    TcpConn *c = tcp_connect(TEST_IP(10,0,2,2), 8000, 49152);
    if (!c) {
        put_text("RESULT FAIL no connection\n");
        qemu_exit(0);
    }

    const char request[] = "GET / HTTP/1.0\r\n\r\n";
    int sent = 0, received = 0, established = 0;
    u8 buffer[256];

    for (u32 loops = 0; loops < 400000; loops++) {
        timer_ticks++;
        net_poll();
        int state = tcp_state(c);
        if (state == TCP_ESTABLISHED) {
            if (!established) { established = 1; put_text("established\n"); }
            if (!sent) {
                sent = tcp_send(c, (const u8 *)request, (int)sizeof(request) - 1);
                if (sent > 0) put_text("sent GET\n");
            }
        }
        if (sent) {
            int n = tcp_recv(c, buffer, (int)sizeof(buffer));
            if (n > 0) {
                for (int i = 0; i < n && received < 220; i++, received++) {
                    put_char((char)buffer[i]);
                }
            }
        }
        if (sent && (received >= 120 || state == TCP_CLOSED)) break;
        for (volatile int delay = 0; delay < 5000; delay++) {}
    }

    put_text("\nreceived=");
    put_num((u32)received);
    put_text(" state=");
    put_num((u32)tcp_state(c));
    put_text("\n");
    if (received > 0 && sent > 0) {
        put_text("RESULT PASS\n");
        qemu_exit(1);
    }
    put_text("RESULT FAIL no response\n");
    qemu_exit(0);
}
