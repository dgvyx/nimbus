#include <stdio.h>
#include "../kernel/net.h"
int main(void) {
    u8 syn[] = {0xc0,0,0x1f,0x40,0x12,0x34,0x56,0x78,0,0,0,0,
                0x60,2,0xff,0xff,0,0,0,0,2,4,5,0xb4,'X'};
    u16 a = tcp_checksum(0x0a00020f, 0x0a000202, syn, 24);
    u16 b = tcp_checksum(0x0a00020f, 0x0a000202, syn, 25);
    printf("SYN checksum: %04x; odd: %04x\n", a, b);
    /* Expected values computed by tests/tcp_vector.py. */
    if (a != 0x3829 || b != 0xe027) return 1;
    syn[16] = a >> 8; syn[17] = a;
    if (tcp_checksum(0x0a00020f, 0x0a000202, syn, 24)) return 2;
    syn[4] ^= 1;
    if (!tcp_checksum(0x0a00020f, 0x0a000202, syn, 24)) return 3;
    puts("TCP checksum passed");
    return 0;
}
