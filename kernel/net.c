/* PCI RTL8139 + Ethernet/ARP/IPv4/ICMP/UDP/DNS/TCP, sem chamadas bloqueantes. */
#include "net.h"

u16 tcp_checksum(u32 src_ip, u32 dst_ip, const u8 *segment, u16 length)
{
    u32 sum = (src_ip >> 16) + (src_ip & 65535) +
              (dst_ip >> 16) + (dst_ip & 65535) + 6 + length;
    unsigned i = 0;
    while (i + 1 < length) {
        sum += ((u16)segment[i] << 8) | segment[i + 1];
        i += 2;
    }
    if (i < length) sum += (u16)segment[i] << 8;
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (u16)~sum;
}

#ifndef NET_CHECKSUM_ONLY

#define IP(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(d))
#define RX_SIZE 8192
static volatile u8 *rx = (volatile u8 *)0x1800000;
static u8 *tx = (u8 *)0x1810000;
static u16 io, rx_offset;
static unsigned tx_slot;
static u32 pci_address;
NetInfo net;
static u8 next_mac[6];
static int neighbor_valid, operation, attempts;
static u32 next_ip, deadline, sent_at, link_checked;
static u16 request_id;
static char dns_name[64];

static u16 be16(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)be16(p) << 16) | be16(p + 2); }
static void put16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = v; }
static void put32(u8 *p, u32 v) { put16(p, v >> 16); put16(p + 2, v); }
static int equal(const u8 *a, const u8 *b, int n) { while (n--) if (*a++ != *b++) return 0; return 1; }
static u16 checksum(const u8 *p, int n)
{
    u32 sum = 0;
    while (n > 1) { sum += be16(p); p += 2; n -= 2; }
    if (n) sum += (u16)*p << 8;
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (u16)~sum;
}
static void status(const char *text)
{
    int i = 0;
    while (text[i] && i < (int)sizeof(net.status) - 1) { net.status[i] = text[i]; i++; }
    net.status[i] = 0; net.revision++;
}
static void finish(const char *text) { net.busy = 0; operation = 0; status(text); }

void net_ip_string(u32 ip, char *text)
{
    int k = 0;
    for (int i = 3; i >= 0; i--) {
        unsigned v = (ip >> (i * 8)) & 255;
        if (v >= 100) text[k++] = '0' + v / 100;
        if (v >= 10) text[k++] = '0' + (v / 10) % 10;
        text[k++] = '0' + v % 10;
        if (i) text[k++] = '.';
    }
    text[k] = 0;
}

static u32 pci_read(u32 address, u8 reg)
{
    outl(0xCF8, address | (reg & 0xFC));
    return inl(0xCFC);
}

static int find_card(void)
{
    for (int bus = 0; bus < 256; bus++) for (int dev = 0; dev < 32; dev++) {
        u32 base = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11);
        if ((pci_read(base, 0) & 65535) == 65535) continue;
        int functions = (pci_read(base, 0x0C) & 0x800000) ? 8 : 1;
        for (int fn = 0; fn < functions; fn++) {
            u32 a = base | ((u32)fn << 8);
            if (pci_read(a, 0) != 0x813910EC) continue;
            u32 bar = pci_read(a, 0x10);
            if (!(bar & 1) || (bar & ~3u) > 65535) continue;
            io = bar & ~3u; pci_address = a;
            return 1;
        }
    }
    return 0;
}

static int card_start(void)
{
    outb(io + 0x52, 0);
    outb(io + 0x37, 0x10);
    int guard = 100000;
    while ((inb(io + 0x37) & 0x10) && --guard) ;
    if (!guard) return 0;
    rx_offset = 0; tx_slot = 0;
    memset((void *)rx, 0, RX_SIZE + 16 + 2048);
    outl(io + 0x30, (u32)rx);
    for (int i = 0; i < 4; i++) outl(io + 0x20 + 4 * i, (u32)(tx + 2048 * i));
    outw(io + 0x3C, 0);             /* polling: sem IRQ da placa */
    outw(io + 0x3E, 0xFFFF);
    outb(io + 0x37, 0x0C);          /* TX/RX */
    outl(io + 0x44, 0x8A | (7 << 8)); /* unicast + broadcast, ring 8K */
    outl(io + 0x40, (3u << 24) | (7 << 8));
    outw(io + 0x38, 0xFFF0);
    return 1;
}

void net_init(void)
{
    net.ip = IP(10,0,2,15); net.gateway = IP(10,0,2,2); net.dns = IP(10,0,2,3);
    if (!find_card()) { status("RTL8139 nao encontrada"); return; }
    u16 command = pci_read(pci_address, 4);
    outl(0xCF8, pci_address | 4); outw(0xCFC, command | 5);
    if (!card_start()) { status("Falha ao iniciar RTL8139"); return; }
    for (int i = 0; i < 6; i++) net.mac[i] = inb(io + i);
    net.present = 1;
    net.link = !(inb(io + 0x58) & 4);
    status(net.link ? "Pronta - perfil QEMU NAT" : "Cabo desconectado");
}

static int ethernet(const u8 *dest, u16 type, const u8 *payload, int size)
{
#ifdef NET_TEST
    extern int test_ethernet(const u8 *, u16, const u8 *, int);
    return test_ethernet(dest, type, payload, size);
#else
    if (!net.present || !net.link || size < 0 || size > 1500) return 0;
    unsigned slot = tx_slot;
    if (!(inl(io + 0x10 + 4 * slot) & (1 << 13))) return 0;
    u8 *frame = tx + slot * 2048;
    memcpy(frame, dest, 6); memcpy(frame + 6, net.mac, 6); put16(frame + 12, type);
    memcpy(frame + 14, payload, size);
    int length = size + 14;
    if (length < 60) { memset(frame + length, 0, 60 - length); length = 60; }
    __asm__ volatile("" ::: "memory");
    outl(io + 0x10 + 4 * slot, (u32)length);
    tx_slot = (slot + 1) & 3; net.tx_packets++;
    return 1;
#endif
}

static void arp_send(u16 op, const u8 *mac, u32 ip)
{
    u8 packet[28] = {0};
    put16(packet, 1); put16(packet + 2, 0x0800); packet[4] = 6; packet[5] = 4;
    put16(packet + 6, op); memcpy(packet + 8, net.mac, 6); put32(packet + 14, net.ip);
    if (op == 2) memcpy(packet + 18, mac, 6);
    put32(packet + 24, ip);
    ethernet(mac, 0x0806, packet, sizeof(packet));
}

static int ipv4(u8 protocol, u32 dest, const u8 *data, int size, const u8 *mac)
{
    u8 packet[1500] = {0};
    if (size > 1480) return 0;
    packet[0] = 0x45; put16(packet + 2, size + 20); put16(packet + 4, request_id);
    packet[8] = 64; packet[9] = protocol;
    put32(packet + 12, net.ip); put32(packet + 16, dest);
    put16(packet + 10, checksum(packet, 20)); memcpy(packet + 20, data, size);
    return ethernet(mac, 0x0800, packet, size + 20);
}

static void transmit_request(void)
{
    int sent = 0;
    if (operation == 1) {
        u8 ping[32] = {8,0,0,0,0x4E,0x49};
        put16(ping + 6, request_id);
        memcpy(ping + 8, "Nimbus network probe", 20);
        put16(ping + 2, checksum(ping, sizeof(ping)));
        sent = ipv4(1, net.gateway, ping, sizeof(ping), next_mac);
    } else if (operation == 2) {
        u8 udp[128] = {0};
        put16(udp, 49152); put16(udp + 2, 53);
        u8 *dns = udp + 8;
        put16(dns, request_id); put16(dns + 2, 0x0100); put16(dns + 4, 1);
        int at = 12, start = 0;
        for (int i = 0;; i++) if (!dns_name[i] || dns_name[i] == '.') {
            dns[at++] = i - start;
            memcpy(dns + at, dns_name + start, i - start); at += i - start;
            if (!dns_name[i]) break;
            start = i + 1;
        }
        dns[at++] = 0; put16(dns + at, 1); at += 2; put16(dns + at, 1); at += 2;
        put16(udp + 4, at + 8);     /* checksum UDP zero permitido em IPv4 */
        sent = ipv4(17, net.dns, udp, at + 8, next_mac);
    }
    if (sent) sent_at = timer_ticks;
    deadline = timer_ticks + TICK_HZ;
}

static void retry_request(void)
{
    if (++attempts > 4) { finish("Tempo limite: sem resposta da rede"); return; }
    if (!neighbor_valid) {
        const u8 broadcast[6] = {255,255,255,255,255,255};
        arp_send(1, broadcast, next_ip);
        deadline = timer_ticks + TICK_HZ;
    } else transmit_request();
}

static void begin(int kind)
{
    if (!net.present) { status("Adicione -nic user,model=rtl8139 ao QEMU"); return; }
    if (!net.link) { status("Cabo desconectado"); return; }
    if (net.busy) return;
    operation = kind; net.busy = 1; attempts = 0; neighbor_valid = 0;
    next_ip = kind == 1 ? net.gateway : net.dns;
    request_id++; net.resolved = 0;
    status(kind == 1 ? "Testando gateway..." : "Consultando DNS...");
    retry_request();
}
void net_ping(void) { begin(1); }
void net_resolve(const char *name)
{
    if (net.busy) return;
    int length = 0, label = 0;
    while (name[length]) {
        char c = name[length];
        if (length >= 63 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.')) { status("Dominio invalido"); return; }
        if (c == '.') { if (!label) { status("Dominio invalido"); return; } label = 0; }
        else label++;
        dns_name[length++] = c;
    }
    if (!label) { status("Digite um dominio, como example.com"); return; }
    dns_name[length] = 0;
    begin(2);
}

/* Avanca um nome DNS sem seguir ponteiros: tamanho limitado ao pacote. */
static int skip_name(const u8 *p, int n, int at)
{
    int total = 0;
    while (at < n && total++ < 128) {
        int size = p[at++];
        if (!size) return at;
        if ((size & 0xC0) == 0xC0) {
            if (at >= n || (((size & 63) << 8) | p[at]) >= n) return -1;
            return at + 1;
        }
        if (size > 63 || at + size > n) return -1;
        at += size;
    }
    return -1;
}

static void dns_receive(const u8 *p, int n)
{
    if (n < 12 || be16(p) != request_id || !(p[2] & 0x80) || (p[2] & 0x78)) return;
    if (p[2] & 2) { finish("Resposta DNS truncada (TCP ainda indisponivel)"); return; }
    if (p[3] & 15) { finish("DNS retornou erro para este dominio"); return; }
    if (be16(p + 4) != 1) { net.dropped++; return; }
    int at = skip_name(p, n, 12);
    if (at < 0 || at + 4 > n || be16(p + at) != 1 || be16(p + at + 2) != 1) { net.dropped++; return; }
    /* Confere o nome consultado, aceitando diferenca de caixa. */
    int pos = 12, namepos = 0;
    while (pos < at && p[pos]) {
        int len = p[pos++];
        if (len > 63 || pos + len > at) return;
        for (int i = 0; i < len; i++) {
            char a = p[pos++], b = dns_name[namepos++];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) return;
        }
        if (p[pos] && dns_name[namepos++] != '.') return;
    }
    if (dns_name[namepos]) return;
    at += 4;
    unsigned answers = be16(p + 6);
    for (unsigned i = 0; i < answers && i < 64; i++) {
        at = skip_name(p, n, at);
        if (at < 0 || at + 10 > n) { net.dropped++; return; }
        int size = be16(p + at + 8);
        if (at + 10 + size > n) { net.dropped++; return; }
        if (be16(p + at) == 1 && be16(p + at + 2) == 1 && size == 4) {
            net.resolved = be32(p + at + 10);
            finish("DNS respondeu - endereco IPv4 recebido"); return;
        }
        at += 10 + size;
    }
    finish("DNS respondeu sem registro IPv4");
}

static void receive(const u8 *frame, int size)
{
    if (size < 14) { net.dropped++; return; }
    u16 type = be16(frame + 12);
    const u8 *p = frame + 14; int n = size - 14;
    if (type == 0x0806) {
        if (n < 28 || be16(p) != 1 || be16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4 || be32(p + 24) != net.ip) return;
        if (!equal(frame + 6, p + 8, 6)) return;
        if (be16(p + 6) == 1) arp_send(2, p + 8, be32(p + 14));
        if (be16(p + 6) == 2 && be32(p + 14) == next_ip && equal(p + 18, net.mac, 6)) {
            memcpy(next_mac, p + 8, 6); neighbor_valid = 1;
            if (net.busy) transmit_request();
        }
        return;
    }
    if (type != 0x0800 || n < 20 || (p[0] >> 4) != 4 || be32(p + 16) != net.ip) return;
    int header = (p[0] & 15) * 4, total = be16(p + 2);
    if (header < 20 || header > n || total < header || total > n || (be16(p + 6) & 0x3FFF) || checksum(p, header)) { net.dropped++; return; }
    const u8 *data = p + header; int length = total - header;
    if (p[9] == 1 && length >= 8 && !checksum(data, length)) {
        if (data[0] == 0 && data[1] == 0 && operation == 1 && be32(p + 12) == net.gateway &&
            be16(data + 4) == 0x4E49 && be16(data + 6) == request_id) {
            net.ping_ms = (timer_ticks - sent_at) * 1000 / TICK_HZ;
            finish("Gateway respondeu ao ping");
        } else if (data[0] == 8 && length <= 1480) {
            u8 reply[1480]; memcpy(reply, data, length); reply[0] = 0; reply[2] = reply[3] = 0;
            put16(reply + 2, checksum(reply, length)); ipv4(1, be32(p + 12), reply, length, frame + 6);
        }
    } else if (p[9] == 17 && length >= 8 && operation == 2 && be32(p + 12) == net.dns &&
        be16(data) == 53 && be16(data + 2) == 49152) {
        int udp_length = be16(data + 4);
        if (udp_length < 8 || udp_length > length) { net.dropped++; return; }
        if (be16(data + 6)) {
            u8 pseudo[1512];
            memcpy(pseudo, p + 12, 8); pseudo[8] = 0; pseudo[9] = 17;
            put16(pseudo + 10, udp_length); memcpy(pseudo + 12, data, udp_length);
            if (checksum(pseudo, udp_length + 12)) { net.dropped++; return; }
        }
        dns_receive(data + 8, udp_length - 8);
    } else if (p[9] == 6) {
        tcp_receive(frame, size);
    }
}

void net_poll(void)
{
    if (!net.present) return;
    if (timer_ticks - link_checked >= TICK_HZ) {
        link_checked = timer_ticks;
        int link = !(inb(io + 0x58) & 4);
        if (link != net.link) {
            net.link = link;
            if (!link) finish("Cabo desconectado"); else status("Pronta - perfil QEMU NAT");
        }
    }
    for (int guard = 0; guard < 32 && !(inb(io + 0x37) & 1); guard++) {
        u16 flags = *(volatile u16 *)(rx + rx_offset);
        u16 size = *(volatile u16 *)(rx + rx_offset + 2);
        if (size == 0xFFF0) break; /* DMA ainda em andamento */
        if (!(flags & 1) || size < 18 || size > 1518) {
            net.dropped++;
            if (!card_start()) { net.present = 0; finish("Falha ao recuperar recepcao"); }
            break;
        }
        u8 packet[1518];
        for (int i = 0; i < size - 4; i++) packet[i] = rx[rx_offset + 4 + i];
        rx_offset = (rx_offset + size + 4 + 3) & ~3;
        rx_offset %= RX_SIZE;
        outw(io + 0x38, rx_offset - 16);
        net.rx_packets++;
        receive(packet, size - 4);
    }
    u16 interrupts = inw(io + 0x3E);
    if (interrupts & 0x50) {
        net.dropped++;
        if (!card_start()) { net.present = 0; finish("Falha ao recuperar recepcao"); }
    }
    outw(io + 0x3E, interrupts);
    if (net.busy && (int)(timer_ticks - deadline) >= 0) retry_request();
    tcp_poll();
}

/* ==================== TCP ==================== */

#define TCP_MAX_CONNS 4
#define TCP_MSS 1460
#define TCP_WINDOW 65535
#define TCP_RTO_INITIAL (TICK_HZ / 10)  /* 24 ticks = 100ms */
#define TCP_RTO_MAX (TICK_HZ * 5)       /* 5 segundos */

typedef struct TcpConn {
    int state;              /* estado da conexao */
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    u32 seq;                /* numero de sequencia a enviar */
    u32 ack;                /* numero de ack esperado */
    u32 remote_seq;         /* sequencia remota recebida */
    u32 remote_ack;         /* ack remoto recebido */
    u8 *send_buf;           /* buffer de envio */
    u32 send_len;           /* dados no buffer de envio */
    u32 send_una;           /* bytes in flight at the front of send_buf */
    u8 *recv_buf;           /* buffer de recepcao */
    u32 recv_len;           /* dados no buffer de recepcao */
    u32 rto;                /* timeout de retransmissao */
    u32 rto_base;           /* RTO base para backoff exponencial */
    u32 last_send;          /* ultimo envio */
    u32 retransmits;        /* contador de retransmissoes */
    int active;             /* conexao ativa */
    int sent;
    u16 remote_window;
    u32 fin_seq;            /* sequence occupied by our FIN */
    u32 close_at;           /* TIME_WAIT expiry */
    int fin_sent;
    int close_pending;
} TcpConn;

static TcpConn tcp_conns[TCP_MAX_CONNS];
static u16 tcp_port_next = 1024;

static u32 tcp_next_hop(u32 dest) {
    /* Static QEMU user profile: 10.0.2.0/24. */
    return (dest & 0xffffff00u) == (net.ip & 0xffffff00u) ? dest : net.gateway;
}

/* TCP flags */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

static u16 tcp_checksum_full(u32 src_ip, u32 dst_ip, const u8 *tcp_hdr, u16 tcp_len) {
    return tcp_checksum(src_ip, dst_ip, tcp_hdr, tcp_len);
}


static int tcp_send_packet(TcpConn *c, u8 flags, const u8 *data, u16 data_len) {
    if (!net.link || !net.present || data_len > TCP_MSS ||
        ((flags & TCP_SYN) && data_len > TCP_MSS - 4) || (data_len && !data)) return 0;
    
    u8 packet[1500] = {0};
    u8 *tcp = packet + 20;  /* ethernet() prepends its own 14-byte header. */
    
    put16(tcp, c->local_port);
    put16(tcp + 2, c->remote_port);
    put32(tcp + 4, c->seq + (data_len || (flags & TCP_SYN) ? 0 : c->send_una));
    put32(tcp + 8, c->ack);
    u16 tcp_hdr_len = (flags & TCP_SYN) ? 24 : 20;
    tcp[12] = (u8)((tcp_hdr_len / 4) << 4);
    tcp[13] = flags;
    put16(tcp + 14, TCP_WINDOW - c->recv_len);
    put16(tcp + 16, 0);  /* checksum placeholder */
    put16(tcp + 18, 0);  /* urgent pointer */
    
    /* Options: MSS */
    if (flags & TCP_SYN) {
        tcp[20] = 2;  /* kind = MSS */
        tcp[21] = 4;
        put16(tcp + 22, TCP_MSS);
    }
    
    if (data && data_len > 0) {
        memcpy(tcp + tcp_hdr_len, data, data_len);
    }
    
    u16 total_tcp_len = tcp_hdr_len + data_len;
    
    /* IP header */
    packet[0] = 0x45;
    put16(packet + 2, total_tcp_len + 20);
    put16(packet + 4, request_id++);
    packet[8] = 64;
    packet[9] = 6;  /* TCP */
    put32(packet + 12, c->local_ip);
    put32(packet + 16, c->remote_ip);
    put16(packet + 10, checksum(packet, 20));
    
    /* TCP checksum with pseudo-header */
    put16(tcp + 16, tcp_checksum_full(c->local_ip, c->remote_ip, tcp, total_tcp_len));
    
    /* Ethernet send - ensure ARP is resolved */
    u8 mac[6];
    u32 target_ip = tcp_next_hop(c->remote_ip);
    if (target_ip != next_ip) {
        /* Need ARP for new target */
        next_ip = target_ip;
        neighbor_valid = 0;
    }
    if (!neighbor_valid) {
        /* ARP not resolved yet - trigger ARP and return */
        const u8 broadcast[6] = {255,255,255,255,255,255};
        arp_send(1, broadcast, next_ip);
        return 0;  /* Will retry on next tcp_poll */
    }
    memcpy(mac, next_mac, 6);
    
    if (!ethernet(mac, 0x0800, packet, total_tcp_len + 20)) return 0;
    c->last_send = timer_ticks;
    c->sent = 1;
    return 1;
}

static void tcp_release(TcpConn *c) {
    if (c->send_buf) kfree(c->send_buf);
    if (c->recv_buf) kfree(c->recv_buf);
    memset(c, 0, sizeof(TcpConn));
    c->state = TCP_CLOSED;
}

void tcp_init(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) tcp_release(&tcp_conns[i]);
}

static void tcp_try_fin(TcpConn *c) {
    if (!c->close_pending || c->fin_sent || c->send_len || c->send_una) return;
    c->close_pending = 0;
    c->fin_seq = c->seq;
    c->fin_sent = 1;
    tcp_send_packet(c, TCP_FIN | TCP_ACK, 0, 0);
}

/* Stop-and-wait: one bounded segment outstanding, sequence is its first byte. */
static void tcp_flush(TcpConn *c) {
    if (!c->send_len) { tcp_try_fin(c); return; }
    if (c->send_una || !c->remote_window) return;
    u32 n = c->send_len;
    if (n > TCP_MSS) n = TCP_MSS;
    if (n > c->remote_window) n = c->remote_window;
    if (tcp_send_packet(c, TCP_ACK | TCP_PSH, c->send_buf, (u16)n)) c->send_una = n;
}

TcpConn *tcp_connect(u32 dest_ip, u16 dest_port, u16 src_port) {
    if (!net.link || !net.present) return 0;
    
    TcpConn *c = 0;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!tcp_conns[i].active) {
            c = &tcp_conns[i];
            break;
        }
    }
    if (!c) return 0;
    
    memset(c, 0, sizeof(TcpConn));
    c->state = TCP_SYN_SENT;
    c->local_ip = net.ip;
    c->remote_ip = dest_ip;
    c->local_port = src_port ? src_port : tcp_port_next++;
    c->remote_port = dest_port;
    c->seq = 0x12345678;  /* ISN simples */
    c->ack = 0;
    c->rto = TCP_RTO_INITIAL;
    c->rto_base = TCP_RTO_INITIAL;
    c->send_buf = (u8 *)kmalloc(TCP_WINDOW);
    c->recv_buf = (u8 *)kmalloc(TCP_WINDOW);
    c->active = 1;
    
    if (!c->send_buf || !c->recv_buf) {
        if (c->send_buf) kfree(c->send_buf);
        if (c->recv_buf) kfree(c->recv_buf);
        c->active = 0;
        return 0;
    }
    
    /* ARP for destination if needed */
    next_ip = tcp_next_hop(dest_ip);
    neighbor_valid = 0;
    
    /* Send SYN */
    tcp_send_packet(c, TCP_SYN, 0, 0);
    
    return c;
}

int tcp_send(TcpConn *c, const u8 *data, int len) {
    if (!c || !c->active || c->state != TCP_ESTABLISHED) return -1;
    if (len <= 0) return 0;
    if (!data) return -1;
    if ((u32)len > TCP_WINDOW - c->send_len) len = TCP_WINDOW - c->send_len;
    if (len <= 0) return 0;
    
    memcpy(c->send_buf + c->send_len, data, len);
    c->send_len += len;
    
    tcp_flush(c);
    return len;
}

int tcp_recv(TcpConn *c, u8 *buf, int maxlen) {
    if (!c || !c->active || !buf || maxlen <= 0 || c->recv_len == 0) return 0;
    u32 to_copy = c->recv_len < (u32)maxlen ? c->recv_len : (u32)maxlen;
    memcpy(buf, c->recv_buf, to_copy);
    /* Shift remaining */
    if (to_copy < c->recv_len) {
        memmove(c->recv_buf, c->recv_buf + to_copy, c->recv_len - to_copy);
    }
    c->recv_len -= to_copy;
    return to_copy;
}

void tcp_close(TcpConn *c) {
    if (!c || !c->active) return;
    if (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT ||
        c->state == TCP_SYN_RCVD) {
        c->state = c->state == TCP_CLOSE_WAIT ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
        c->close_pending = 1;
        tcp_try_fin(c);   /* wait for outstanding data to be acknowledged */
    } else {
        tcp_release(c);
    }
}

int tcp_state(TcpConn *c) {
    if (!c) return TCP_CLOSED;
    return c->state;
}


static void tcp_handle_syn_ack(TcpConn *c, const u8 *tcp) {
    if (c->state != TCP_SYN_SENT) return;
    u32 ack = be32(tcp + 8);
    u32 seq = be32(tcp + 4);
    u8 flags = tcp[13];
    
    if (!(flags & TCP_ACK) || !(flags & TCP_SYN)) return;
    if (ack != c->seq + 1) return;
    
    c->remote_seq = seq + 1;
    c->ack = c->remote_seq;
    c->seq = ack;
    c->remote_window = be16(tcp + 14);
    c->retransmits = 0;
    c->rto = TCP_RTO_INITIAL;
    c->state = TCP_ESTABLISHED;
    
    /* Send ACK for SYN-ACK */
    tcp_send_packet(c, TCP_ACK, 0, 0);
}

static int tcp_receiving(int state) {
    return state == TCP_ESTABLISHED || state == TCP_CLOSE_WAIT ||
           state == TCP_FIN_WAIT_1 || state == TCP_FIN_WAIT_2;
}

static void tcp_handle_data(TcpConn *c, const u8 *tcp, u16 tcp_len, u16 hdr_len) {
    if (!tcp_receiving(c->state)) return;
    u32 seq = be32(tcp + 4);
    u16 data_len = tcp_len - hdr_len;
    if (!data_len) return;
    if (seq == c->ack) {
        u16 room = (u16)(TCP_WINDOW - c->recv_len);
        u16 take = data_len < room ? data_len : room;
        if (take) {
            memcpy(c->recv_buf + c->recv_len, tcp + hdr_len, take);
            c->recv_len += take;
            c->ack += take;
        }
        tcp_send_packet(c, TCP_ACK, 0, 0);
    } else {
        /* Duplicate or out of order: repeat our cumulative acknowledgement. */
        tcp_send_packet(c, TCP_ACK, 0, 0);
    }
}

static void tcp_handle_ack(TcpConn *c, const u8 *tcp) {
    u32 ack = be32(tcp + 8);
    u32 count = ack - c->seq;
    if (count <= c->send_una) {
        c->remote_window = be16(tcp + 14);
        if (count) {
            c->seq = ack;
            c->send_una -= count;
            c->send_len -= count;
            if (c->send_len) memmove(c->send_buf, c->send_buf + count, c->send_len);
            c->retransmits = 0;
            c->rto = TCP_RTO_INITIAL;
        }
    } else if (c->fin_sent && ack == c->fin_seq + 1) {
        c->fin_sent = 0;
        if (c->state == TCP_FIN_WAIT_1) {
            c->state = TCP_FIN_WAIT_2;
        } else if (c->state == TCP_CLOSING || c->state == TCP_LAST_ACK) {
            tcp_release(c);
            return;
        }
    } else {
        return;
    }
    if (c->close_pending && !c->send_len && !c->send_una) tcp_try_fin(c);
    tcp_flush(c);
}

static void tcp_handle_fin(TcpConn *c, u32 seq, u16 data_len) {
    u32 end = seq + data_len;
    if (end != c->ack) {
        /* Duplicate FIN or data gap: replay the cumulative acknowledgement. */
        tcp_send_packet(c, TCP_ACK, 0, 0);
        return;
    }
    c->ack = end + 1;
    if (c->state == TCP_ESTABLISHED) {
        c->state = TCP_CLOSE_WAIT;
    } else if (c->state == TCP_FIN_WAIT_1) {
        c->state = TCP_CLOSING;
    } else if (c->state == TCP_FIN_WAIT_2) {
        c->state = TCP_TIME_WAIT;
        c->close_at = timer_ticks + TICK_HZ;
    }
    tcp_send_packet(c, TCP_ACK, 0, 0);
}

static void tcp_process_packet(TcpConn *c, const u8 *tcp, u16 length) {
    u8 flags = tcp[13];
    u16 header = (tcp[12] >> 4) * 4;
    u32 seq = be32(tcp + 4);
    u16 data_len = length > header ? length - header : 0;

    if (flags & TCP_RST) {
        int valid = c->state == TCP_SYN_SENT ?
            ((flags & TCP_ACK) && be32(tcp + 8) == c->seq + 1) : seq == c->ack;
        if (valid) tcp_release(c);
        return;
    }
    if (c->state == TCP_SYN_SENT) {
        tcp_handle_syn_ack(c, tcp);
        return;
    }
    if (flags & TCP_SYN) { tcp_send_packet(c, TCP_ACK, 0, 0); return; }
    if (flags & TCP_ACK) {
        tcp_handle_ack(c, tcp);
        if (!c->active) return;
    }
    if (data_len) tcp_handle_data(c, tcp, length, header);
    if (flags & TCP_FIN) tcp_handle_fin(c, seq, data_len);
}

void tcp_poll(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        TcpConn *c = &tcp_conns[i];
        if (!c->active) continue;
        
        /* Check timeouts */
        if (c->state == TCP_TIME_WAIT) {
            if ((int)(timer_ticks - c->close_at) >= 0) tcp_release(c);
            continue;
        }
        if (c->state == TCP_SYN_SENT) {
            /* If ARP now resolved, send SYN immediately */
            if (!c->sent && neighbor_valid && tcp_next_hop(c->remote_ip) == next_ip) {
                tcp_send_packet(c, TCP_SYN, 0, 0);
            } else if (timer_ticks - c->last_send >= c->rto) {
                if (++c->retransmits > 5) { tcp_release(c); continue; }
                c->rto = c->rto * 2;
                if (c->rto > TCP_RTO_MAX) c->rto = TCP_RTO_MAX;
                tcp_send_packet(c, TCP_SYN, 0, 0);
            }
        } else if (c->send_una && timer_ticks - c->last_send >= c->rto) {
            if (++c->retransmits > 10) { tcp_release(c); continue; }
            c->rto = c->rto * 2;
            if (c->rto > TCP_RTO_MAX) c->rto = TCP_RTO_MAX;
            tcp_send_packet(c, TCP_ACK | TCP_PSH, c->send_buf, (u16)c->send_una);
        } else if (c->fin_sent &&
                   (c->state == TCP_FIN_WAIT_1 || c->state == TCP_CLOSING ||
                    c->state == TCP_LAST_ACK) &&
                   timer_ticks - c->last_send >= c->rto) {
            if (++c->retransmits > 5) { tcp_release(c); continue; }
            c->rto = c->rto * 2;
            if (c->rto > TCP_RTO_MAX) c->rto = TCP_RTO_MAX;
            tcp_send_packet(c, TCP_FIN | TCP_ACK, 0, 0);
        } else {
            tcp_flush(c);
        }
    }
}

/* TCP receive integration - call from net receive when protocol == 6 */
void tcp_receive(const u8 *frame, int size) {
    if (size < 54 || be16(frame + 12) != 0x0800) return;
    
    /* Skip Ethernet (14) */
    const u8 *ip = frame + 14;
    if ((ip[0] >> 4) != 4) return;
    if (ip[9] != 6) return;  /* Not TCP */
    
    u32 src_ip = be32(ip + 12);
    u32 dst_ip = be32(ip + 16);
    int ip_hdr_len = (ip[0] & 15) * 4;
    int ip_total = be16(ip + 2);
    if (ip_hdr_len < 20 || ip_hdr_len > size - 14 ||
        ip_total < ip_hdr_len + 20 || ip_total > size - 14 ||
        (be16(ip + 6) & 0x3fff) || checksum(ip, ip_hdr_len)) return;
    
    const u8 *tcp = ip + ip_hdr_len;
    int tcp_len = ip_total - ip_hdr_len;
    int header = (tcp[12] >> 4) * 4;
    if (header < 20 || header > tcp_len ||
        tcp_checksum_full(src_ip, dst_ip, tcp, (u16)tcp_len)) return;
    
    /* Find connection */
    u16 src_port = be16(tcp);
    u16 dst_port = be16(tcp + 2);
    
    TcpConn *c = 0;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (tcp_conns[i].active &&
            tcp_conns[i].local_port == dst_port &&
            tcp_conns[i].remote_port == src_port &&
            tcp_conns[i].local_ip == dst_ip &&
            tcp_conns[i].remote_ip == src_ip) {
            c = &tcp_conns[i];
            break;
        }
    }
    
    if (!c) {
        /* No connection - send RST if SYN */
        if (tcp[13] & TCP_SYN) {
            /* Could implement passive open here */
        }
        return;
    }
    
    tcp_process_packet(c, tcp, tcp_len);
}
#endif
