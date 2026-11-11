/* Nimbus OS - cliente HTTP/1.0 simples sobre a pilha TCP. */
#include "nimbus.h"
#include "http.h"

/* ==================== funcoes puras (testaveis no PC) ==================== */

static int ci_prefix(const char *text, const char *prefix)
{
    while (*prefix) {
        char a = *text++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static int host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.';
}

int http_parse_url(const char *url, char *host, int host_size, u16 *port,
                   char *path, int path_size)
{
    if (!url || !host || !port || !path || host_size < 2 || path_size < 2) return 0;
    if (!ci_prefix(url, "http://")) return 0;
    const char *p = url + 7;
    if (!*p) return 0;

    const char *authority = p;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    int alen = (int)(p - authority);
    if (alen <= 0) return 0;

    const char *colon = 0;
    for (int i = 0; i < alen; i++) {
        if (authority[i] == ':') {
            if (colon) return 0;      /* IPv6 nao suportado */
            colon = authority + i;
        }
    }
    int hlen = colon ? (int)(colon - authority) : alen;
    if (hlen <= 0 || hlen >= host_size) return 0;
    for (int i = 0; i < hlen; i++) if (!host_char(authority[i])) return 0;
    for (int i = 0; i < hlen; i++) host[i] = authority[i];
    host[hlen] = 0;

    u16 parsed_port = 80;
    if (colon) {
        const char *q = colon + 1;
        const char *end = authority + alen;
        int digits = 0;
        int value = 0;
        if (q >= end) return 0;
        while (q < end) {
            if (*q < '0' || *q > '9') return 0;
            value = value * 10 + (*q - '0');
            if (value > 65535) return 0;
            digits++;
            q++;
        }
        if (!digits || !value) return 0;
        parsed_port = (u16)value;
    }
    *port = parsed_port;

    int k = 0;
    if (*p == '\0' || *p == '#') {
        path[0] = '/';
        path[1] = 0;
        return 1;
    }
    if (*p == '?') path[k++] = '/';
    while (*p && *p != '#') {
        unsigned char c = (unsigned char)*p++;
        if (c <= 32 || c == 127) return 0;
        if (k >= path_size - 1) return 0;
        path[k++] = (char)c;
    }
    if (!k) {
        if (path_size < 2) return 0;
        path[k++] = '/';
    }
    path[k] = 0;
    return 1;
}

int http_parse_ipv4(const char *text, u32 *ip)
{
    u32 value = 0, result = 0;
    int parts = 0, digits = 0;
    const char *p = text;
    if (!text || !ip) return -1;
    for (;;) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            value = value * 10 + (u32)(c - '0');
            if (value > 255) return -1;
            digits++;
            if (digits > 3) return -1;
            p++;
        } else if (c == '.') {
            if (!digits || parts == 3) return -1;
            result = (result << 8) | value;
            parts++;
            value = 0;
            digits = 0;
            p++;
        } else {
            break;
        }
    }
    if (*p) return 0;                 /* tem letras: e um nome DNS */
    if (parts != 3 || !digits) return -1;
    *ip = (result << 8) | value;
    return 1;
}

int http_find_body(const u8 *data, int len)
{
    if (!data || len < 2) return -1;
    for (int i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') return i + 4;
    }
    for (int i = 0; i + 1 < len; i++) {
        if (data[i] == '\n' && data[i + 1] == '\n') return i + 2;
    }
    return -1;
}

int http_parse_status(const u8 *data, int len)
{
    if (!data || len < 12) return 0;
    if (data[0] != 'H' || data[1] != 'T' || data[2] != 'T' ||
        data[3] != 'P' || data[4] != '/') return 0;
    int i = 5;
    while (i < len && data[i] != ' ') i++;
    while (i < len && data[i] == ' ') i++;
    if (i + 3 > len) return 0;
    for (int k = 0; k < 3; k++) if (data[i + k] < '0' || data[i + k] > '9') return 0;
    return (data[i] - '0') * 100 + (data[i + 1] - '0') * 10 + (data[i + 2] - '0');
}

int http_redirect_http(const char *location)
{
    return location && ci_prefix(location, "http://");
}

static int append_text(char *out, int cap, int at, const char *text)
{
    while (*text) {
        if (at >= cap - 1) return -1;
        out[at++] = *text++;
    }
    out[at] = 0;
    return at;
}

int http_build_request(const char *host, u16 port, const char *path,
                       char *out, int cap)
{
    if (!host || !path || !out || cap < 32) return 0;
    int n = append_text(out, cap, 0, "GET ");
    if (n < 0) return 0;
    n = append_text(out, cap, n, path);
    if (n < 0) return 0;
    n = append_text(out, cap, n, " HTTP/1.0\r\nHost: ");
    if (n < 0) return 0;
    n = append_text(out, cap, n, host);
    if (n < 0) return 0;
    if (port != 80) {
        char digits[6];
        int d = 0;
        unsigned value = port;
        do { digits[d++] = (char)('0' + value % 10); value /= 10; } while (value);
        if (n >= cap - 1) return 0;
        out[n++] = ':';
        while (d) {
            if (n >= cap - 1) return 0;
            out[n++] = digits[--d];
        }
        out[n] = 0;
    }
    n = append_text(out, cap, n, "\r\nConnection: close\r\n\r\n");
    return n < 0 ? 0 : n;
}

/* ==================== cliente assincrono ==================== */

#ifndef HTTP_PARSER_ONLY

#include "net.h"

enum { H_IDLE, H_DNS, H_CONNECT, H_RECV };

#define HTTP_DNS_TIMEOUT     (TICK_HZ * 5)
#define HTTP_CONNECT_TIMEOUT (TICK_HZ * 5)
#define HTTP_RECV_TIMEOUT    (TICK_HZ * 10)

typedef struct HttpReq {
    int busy, state, redirects;
    u32 ip;
    u16 port;
    char host[64];
    char path[200];
    u8 *buf;
    int len;
    u32 deadline;
    TcpConn *conn;
    void (*cb)(int status, const u8 *body, int len, void *arg);
    void *arg;
} HttpReq;

static HttpReq http_request;
static char http_error_text[96];

static void http_set_error(const char *text)
{
    int i = 0;
    if (!text) text = "";
    while (text[i] && i < (int)sizeof(http_error_text) - 1) {
        http_error_text[i] = text[i];
        i++;
    }
    http_error_text[i] = 0;
}

const char *http_last_error(void) { return http_error_text; }

static void http_release(void)
{
    HttpReq *r = &http_request;
    if (r->conn) { tcp_close(r->conn); r->conn = 0; }
    if (r->buf) { kfree(r->buf); r->buf = 0; }
    r->len = 0;
    r->state = H_IDLE;
    r->busy = 0;
}

static void http_abort(const char *message)
{
    HttpReq *r = &http_request;
    http_set_error(message);
    if (r->cb) r->cb(0, 0, 0, r->arg);
    http_release();
}

static void http_connect(void)
{
    HttpReq *r = &http_request;
    r->conn = tcp_connect(r->ip, r->port, 0);
    if (!r->conn) { http_abort("sem conexao TCP (link ou slots)"); return; }
    r->state = H_CONNECT;
    r->deadline = timer_ticks + HTTP_CONNECT_TIMEOUT;
}

static void http_open(const char *host, u16 port, const char *path)
{
    HttpReq *r = &http_request;
    int i = 0;
    while (host[i] && i < (int)sizeof(r->host) - 1) { r->host[i] = host[i]; i++; }
    r->host[i] = 0;
    i = 0;
    while (path[i] && i < (int)sizeof(r->path) - 1) { r->path[i] = path[i]; i++; }
    r->path[i] = 0;
    r->port = port;

    int kind = http_parse_ipv4(r->host, &r->ip);
    if (kind < 0) { http_abort("endereco IPv4 invalido"); return; }
    if (kind == 0) {
        if (net.busy) { http_abort("rede ocupada por ping ou DNS"); return; }
        net_resolve(r->host);
        if (!net.busy && !net.resolved) { http_abort("DNS nao resolveu o host"); return; }
        r->state = H_DNS;
        r->deadline = timer_ticks + HTTP_DNS_TIMEOUT;
        return;
    }
    http_connect();
}

static int ci_ncmp(const u8 *text, const char *name, int len)
{
    for (int i = 0; i < len; i++) {
        char a = (char)text[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 1;
    }
    return 0;
}

/* Procura um cabecalho dentro dos primeiros `end` bytes e copia o valor. */
static int http_header_value(const u8 *buf, int end, const char *name,
                             char *out, int cap)
{
    int nlen = 0;
    while (name[nlen]) nlen++;
    int i = 0;
    while (i < end && buf[i] != '\n') i++;
    while (i < end) {
        i++;                              /* pula o '\n' da linha anterior */
        int start = i;
        while (i < end && buf[i] != '\n') i++;
        int stop = i;
        int line_end = stop;
        if (line_end > start && buf[line_end - 1] == '\r') line_end--;
        if (line_end - start > nlen &&
            ci_ncmp(buf + start, name, nlen) == 0 && buf[start + nlen] == ':') {
            int v = start + nlen + 1;
            while (v < line_end && (buf[v] == ' ' || buf[v] == '\t')) v++;
            int w = line_end;
            while (w > v && (buf[w - 1] == ' ' || buf[w - 1] == '\t')) w--;
            int len = w - v;
            if (len >= cap) len = cap - 1;
            if (len < 0) len = 0;
            for (int k = 0; k < len; k++) out[k] = (char)buf[v + k];
            out[len] = 0;
            return len;
        }
    }
    return -1;
}

static void http_complete(void)
{
    HttpReq *r = &http_request;
    int body = -1, status = 0;
    if (r->buf) {
        if (r->len < HTTP_MAX_RESPONSE) r->buf[r->len] = 0;
        body = http_find_body(r->buf, r->len);
        status = http_parse_status(r->buf, r->len);
    }
    if (body < 0) { http_abort("resposta HTTP sem cabecalho completo"); return; }
    if (status <= 0) { http_abort("resposta nao e HTTP"); return; }

    if (status == 301 || status == 302) {
        char location[512];
        int got = http_header_value(r->buf, body, "Location", location, sizeof(location));
        if (got > 0 && http_redirect_http(location)) {
            if (r->redirects >= HTTP_MAX_REDIRECTS) {
                http_abort("muitos redirecionamentos");
                return;
            }
            char host[64], path[200];
            u16 port;
            if (!http_parse_url(location, host, sizeof(host), &port,
                                path, sizeof(path))) {
                http_abort("Location invalido");
                return;
            }
            if (r->conn) { tcp_close(r->conn); r->conn = 0; }
            r->len = 0;
            r->redirects++;
            http_open(host, port, path);
            return;
        }
        /* 301/302 com Location nao-http: entregue ao chamador, sem seguir. */
    }

    if (r->cb) r->cb(status, r->buf + body, r->len - body, r->arg);
    http_release();
}

HttpReq *http_get(const char *url, void (*cb)(int status, const u8 *body,
                  int len, void *arg), void *arg)
{
    HttpReq *r = &http_request;
    char host[64], path[200];
    u16 port;
    http_error_text[0] = 0;
    if (r->busy) { http_set_error("requisicao HTTP ja em andamento"); return 0; }
    if (!net.present) { http_set_error("sem placa de rede"); return 0; }
    if (!net.link) { http_set_error("sem link de rede"); return 0; }
    if (!http_parse_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        http_set_error("URL invalida (use http://host[:porta]/caminho)");
        return 0;
    }

    memset(r, 0, sizeof(*r));
    r->cb = cb;
    r->arg = arg;
    r->buf = (u8 *)kmalloc(HTTP_MAX_RESPONSE);
    if (!r->buf) { http_set_error("sem memoria para a resposta"); return 0; }
    r->busy = 1;
    http_open(host, port, path);
    return r;
}

void http_cancel(void)
{
    if (http_request.busy) http_release();
}

int http_busy(void) { return http_request.busy; }

void http_poll(void)
{
    HttpReq *r = &http_request;
    if (!r->busy) return;

    if (r->state == H_DNS) {
        if (net.resolved) {
            r->ip = net.resolved;
            http_connect();
        } else if (!net.busy) {
            http_abort("DNS nao resolveu o host");
        } else if ((int)(timer_ticks - r->deadline) >= 0) {
            http_abort("timeout no DNS");
        }
        return;
    }

    if (r->state == H_CONNECT) {
        int state = tcp_state(r->conn);
        if (state == TCP_ESTABLISHED) {
            char request[512];
            int length = http_build_request(r->host, r->port, r->path,
                                            request, sizeof(request));
            if (length <= 0) { http_abort("requisicao grande demais"); return; }
            if (tcp_send(r->conn, (const u8 *)request, length) != length) {
                http_abort("falha ao enviar o GET");
                return;
            }
            r->state = H_RECV;
            r->deadline = timer_ticks + HTTP_RECV_TIMEOUT;
        } else if (state == TCP_CLOSED) {
            http_abort("conexao recusada ou sem SYN-ACK");
        } else if ((int)(timer_ticks - r->deadline) >= 0) {
            http_abort("timeout ao conectar");
        }
        return;
    }

    if (r->state == H_RECV) {
        int room = HTTP_MAX_RESPONSE - 1 - r->len;
        int got = room > 0 ? tcp_recv(r->conn, r->buf + r->len, room) : 0;
        if (got > 0) r->len += got;
        int state = tcp_state(r->conn);
        if (state == TCP_CLOSED) { http_complete(); return; }
        if (got == 0 && (state == TCP_CLOSE_WAIT || state == TCP_CLOSING ||
                         state == TCP_LAST_ACK || state == TCP_TIME_WAIT)) {
            http_complete();
            return;
        }
        if (r->len >= HTTP_MAX_RESPONSE - 1) { http_complete(); return; }
        if ((int)(timer_ticks - r->deadline) >= 0) http_complete();
    }
}

#endif /* HTTP_PARSER_ONLY */
