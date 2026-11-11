#ifndef NET_H
#define NET_H
#include "nimbus.h"

/* Perfil estatico da rede user/NAT padrao do QEMU. */
typedef struct {
    int present, link, busy;
    u8 mac[6];
    u32 ip, gateway, dns, resolved;
    u32 tx_packets, rx_packets, dropped, ping_ms, revision;
    char status[96];
} NetInfo;
extern NetInfo net;
void net_init(void);
void net_poll(void);
void net_ping(void);
void net_resolve(const char *name);
void net_ip_string(u32 ip, char *text);

/* TCP */
/* IPs as numeric host values (10.0.2.15 = 0x0a00020f). Segment is wire bytes.
 * Return value is numeric: serialize big-endian. Zero checksum bytes before
 * generating; a complete valid segment including checksum returns zero. */
u16 tcp_checksum(u32 src_ip, u32 dst_ip, const u8 *segment, u16 length);
typedef struct TcpConn TcpConn;
enum { TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RCVD, TCP_ESTABLISHED,
       TCP_FIN_WAIT_1, TCP_FIN_WAIT_2, TCP_CLOSE_WAIT, TCP_CLOSING, TCP_LAST_ACK, TCP_TIME_WAIT };
TcpConn *tcp_connect(u32 dest_ip, u16 dest_port, u16 src_port);
int tcp_send(TcpConn *c, const u8 *data, int len);
int tcp_recv(TcpConn *c, u8 *buf, int maxlen);
void tcp_close(TcpConn *c);
int tcp_state(TcpConn *c);
void tcp_poll(void);
void tcp_receive(const u8 *frame, int size);

/* HTTP */
typedef struct HttpReq HttpReq;
HttpReq *http_get(const char *url, void (*cb)(int status, const u8 *body, int len, void *arg), void *arg);
void http_poll(void);

#endif
