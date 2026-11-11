#include <stdio.h>
#define NET_TEST
#include "../kernel/net.c"
volatile u32 timer_ticks;
static u8 captured[1500], captured_mac[6];
static int captured_len;
static int fail_tx;
void *memcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src; while (n--) *d++ = *s++; return dst;
}
void *memset(void *dst, int c, u32 n) {
    u8 *d = dst; while (n--) *d++ = c; return dst;
}
void *memmove(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src;
    if (d > s) { while (n) { n--; d[n] = s[n]; } }
    else memcpy(dst, src, n);
    return dst;
}
static u8 heap[8][65535];
static unsigned allocation;
void *kmalloc(u32 n) { return n <= 65535 && allocation < 8 ? heap[allocation++] : 0; }
void kfree(void *p) { (void)p; }
int test_ethernet(const u8 *mac, u16 type, const u8 *data, int len) {
    if (fail_tx) return 0;
    if (type == 0x0800) {
        memcpy(captured, data, len); memcpy(captured_mac, mac, 6); captured_len = len;
    }
    return 1;
}
static void dump_pcap(const char *path) {
    static const u8 gh[24] = {0xd4,0xc3,0xb2,0xa1,2,0,4,0, 0,0,0,0,0,0,0,0,
                               0xff,0xff,0,0, 1,0,0,0};
    u8 eth[14], rh[16] = {0};
    memcpy(eth, captured_mac, 6); memcpy(eth + 6, net.mac, 6);
    put16(eth + 12, 0x0800);
    put32(rh + 8, 14 + captured_len); put32(rh + 12, 14 + captured_len);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(gh, 1, sizeof(gh), f); fwrite(rh, 1, sizeof(rh), f);
    fwrite(eth, 1, sizeof(eth), f); fwrite(captured, 1, captured_len, f);
    fclose(f);
}

#define CHECK(x) do { if (!(x)) { printf("line %d failed\n", __LINE__); return 1; } } while (0)
int main(void) {
    TcpConn c;
    memset(&c, 0, sizeof(c));
    net.present = net.link = 1;
    net.ip = c.local_ip = 0x0a00020f;
    net.gateway = next_ip = c.remote_ip = 0x0a000202;
    neighbor_valid = 1;
    next_mac[0] = 0x52; next_mac[1] = 0x54; next_mac[2] = 0x00;
    next_mac[3] = 0x12; next_mac[4] = 0x34; next_mac[5] = 0x56;
    net.mac[0] = 0x52; net.mac[5] = 0x55;
    c.local_port = 49152; c.remote_port = 8000; c.seq = 0x12345678;
    tcp_send_packet(&c, TCP_SYN, 0, 0);
    CHECK(captured_len == 44 && captured[0] == 0x45);
    CHECK(be16(captured + 2) == 44 && !checksum(captured, 20));
    CHECK(captured[8] == 64 && captured[9] == 6);
    CHECK(be16(captured + 20) == 49152 && be16(captured + 22) == 8000);
    CHECK(!tcp_checksum(c.local_ip, c.remote_ip, captured + 20, 24));
    CHECK(equal(captured_mac, next_mac, 6));
    CHECK(captured[32] == 0x60 && captured[33] == 0x02);
    CHECK(be32(captured + 24) == c.seq && !be32(captured + 28));
    CHECK(be16(captured + 34) == 65535 && be16(captured + 36) == 0x3829);
    CHECK(captured[40] == 2 && captured[41] == 4 && be16(captured + 42) == 1460);
    dump_pcap("tests/syn_frame.pcap");
    tcp_send_packet(&c, TCP_ACK, 0, 0);
    CHECK(captured_len == 40 && captured[32] == 0x50 && captured[33] == 0x10);
    CHECK(!tcp_checksum(c.local_ip, c.remote_ip, captured + 20, 20));
    u8 arp[42] = {0};
    memcpy(arp + 6, next_mac, 6); put16(arp + 12, 0x0806);
    put16(arp + 14, 1); put16(arp + 16, 0x0800);
    arp[18] = 6; arp[19] = 4; put16(arp + 20, 2);
    memcpy(arp + 22, next_mac, 6); put32(arp + 28, net.gateway);
    memcpy(arp + 32, net.mac, 6); put32(arp + 38, net.ip);
    neighbor_valid = net.busy = 0;
    receive(arp, sizeof(arp));
    CHECK(neighbor_valid);
    CHECK(tcp_next_hop(0x08080808) == net.gateway);
    CHECK(!tcp_send_packet(&c, TCP_ACK, captured, 1461));
    timer_ticks = 100; fail_tx = 1;
    CHECK(!tcp_send_packet(&c, TCP_ACK, 0, 0) && c.last_send == 0);
    fail_tx = 0;
    TcpConn *conn = tcp_connect(net.gateway, 8000, 49152);
    CHECK(conn != 0);
    neighbor_valid = 1; tcp_poll();
    u8 reply[54] = {0};
    put16(reply + 12, 0x0800);
    u8 *ip = reply + 14, *tcp = ip + 20;
    ip[0] = 0x45; ip[8] = 64; ip[9] = 6; put16(ip + 2, 40);
    put32(ip + 12, net.gateway); put32(ip + 16, net.ip);
    put16(ip + 10, checksum(ip, 20));
    put16(tcp, 8000); put16(tcp + 2, 49152);
    put32(tcp + 4, 1000); put32(tcp + 8, conn->seq + 1);
    tcp[12] = 0x50; tcp[13] = 0x12; put16(tcp + 14, 4096);
    put16(tcp + 16, tcp_checksum(net.gateway, net.ip, tcp, 20));
    tcp[4] ^= 1; receive(reply, sizeof(reply));
    CHECK(tcp_state(conn) == TCP_SYN_SENT);
    tcp[4] ^= 1; receive(reply, sizeof(reply) - 1);
    CHECK(tcp_state(conn) == TCP_SYN_SENT);
    receive(reply, sizeof(reply));
    CHECK(tcp_state(conn) == TCP_ESTABLISHED && conn->ack == 1001);
    const char get[] = "GET / HTTP/1.0\r\n\r\n";
    u32 first = conn->seq;
    CHECK(tcp_send(conn, (const u8 *)get, sizeof(get) - 1) == sizeof(get) - 1);
    CHECK(captured_len == 40 + sizeof(get) - 1 && captured[33] == 0x18);
    CHECK(equal(captured + 40, (const u8 *)get, sizeof(get) - 1));
    CHECK(be32(captured + 24) == first && conn->send_una == sizeof(get) - 1);
    CHECK(!tcp_checksum(net.ip, net.gateway, captured + 20, captured_len - 20));
    timer_ticks += conn->rto; tcp_poll();
    CHECK(be32(captured + 24) == first);
    tcp[13] = TCP_ACK; put32(tcp + 4, 1001);
    put32(tcp + 8, first + sizeof(get) - 1);
    put16(tcp + 16, 0); put16(tcp + 16, tcp_checksum(net.gateway, net.ip, tcp, 20));
    receive(reply, sizeof(reply));
    CHECK(!conn->send_len && !conn->send_una && conn->seq == first + sizeof(get) - 1);
    u8 response[80] = {0};
    memcpy(response, reply, sizeof(reply));
    const char body[] = "HTTP/1.0 200 OK\r\n\r\n";
    int body_len = sizeof(body) - 1;
    memcpy(response + 54, body, body_len);
    put16(response + 16, 40 + body_len);
    put16(response + 24, 0); put16(response + 24, checksum(response + 14, 20));
    put16(response + 50, 0);
    put16(response + 50, tcp_checksum(net.gateway, net.ip, response + 34, 20 + body_len));
    receive(response, 54 + body_len);
    u8 received[40];
    CHECK(tcp_recv(conn, received, -1) == 0);
    CHECK(tcp_recv(conn, received, sizeof(received)) == body_len);
    CHECK(equal(received, (const u8 *)body, body_len));
    tcp_close(conn);
    CHECK(conn->state == TCP_FIN_WAIT_1 && captured[33] == (TCP_FIN | TCP_ACK));
    u32 fin_seq = be32(captured + 24);
    put32(tcp + 4, 1001 + body_len); tcp[13] = TCP_ACK;
    put32(tcp + 8, fin_seq + 1); put16(tcp + 16, 0);
    put16(tcp + 16, tcp_checksum(net.gateway, net.ip, tcp, 20));
    receive(reply, sizeof(reply));
    CHECK(conn->state == TCP_FIN_WAIT_2);
    tcp[13] = TCP_FIN | TCP_ACK; put16(tcp + 16, 0);
    put16(tcp + 16, tcp_checksum(net.gateway, net.ip, tcp, 20));
    receive(reply, sizeof(reply));
    CHECK(conn->state == TCP_TIME_WAIT && conn->active && conn->ack == 1002 + body_len);
    timer_ticks += TICK_HZ; tcp_poll();
    CHECK(!conn->active && tcp_state(conn) == TCP_CLOSED);
    puts("TCP packet placement passed");
    return 0;
}
