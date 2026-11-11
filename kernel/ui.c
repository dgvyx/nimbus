/* Nimbus OS - desktop: barra de menu, dock, janelas e aplicativos */
#include "nimbus.h"
#include "net.h"
#include "http.h"

#define MENUH  30
#define TITLEH 34
#define TS     2
#define CW     12          /* avanco de um caractere (5x7 escala 2 + espaco) */
#define LH     20          /* altura de linha */
#define WHITE  0xFFFFFF

enum { APP_TERM, APP_NOTES, APP_CALC, APP_ABOUT, APP_SETTINGS, APP_NET, NAPPS };

typedef struct {
    int x, y, w, h;
    int open, min, zoom;
    int sx, sy, sw, sh;
    const char *title;
} Win;

static Win wins[NAPPS];
static int zorder[NAPPS], zn;
static int mx, my, mbtn;
static int drag = -1, dragdx, dragdy;
static int drag_cached;
static u32 *drag_background = (u32 *)0x1000000;
static u32 *drag_surface = (u32 *)0x1400000;
static int drag_surface_w, drag_surface_h;
static int dock_hot;
static int menu = -1, menu_hover = -1, menu_x;
static int search_open, search_len, search_selected;
static char search_query[40];
static int damage_x0, damage_y0, damage_x1, damage_y1;
int ui_dirty = 1;
static int clk_h, clk_m, clk_d, clk_mo;
static int appearance_palette, dock_animation = 1;
static u32 accent = 0x3478E5;
static char network_domain[64] = "example.com";
static int network_domain_len = 11;
static u32 last_net_revision, frames_previous, frames_per_second;
static void make_wallpaper(void);

int ui_mouse_x(void) { return mx; }
int ui_mouse_y(void) { return my; }

/* ------------------------------------------------------------ helpers */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static int iabs(int v) { return v < 0 ? -v : v; }
void itoa_(int v, char *o)
{
    char t[12]; int n = 0, k = 0;
    if (v < 0) { o[k++] = '-'; v = -v; }
    if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = '0' + v % 10; v /= 10; }
    while (n > 0) o[k++] = t[--n];
    o[k] = 0;
}
static void put2(char *o, int v) { o[0] = '0' + (v / 10) % 10; o[1] = '0' + v % 10; }

static void damage(int x, int y, int w, int h)
{
    if (ui_dirty & 1) return;
    if (!(ui_dirty & 4)) {
        damage_x0 = x; damage_y0 = y; damage_x1 = x + w; damage_y1 = y + h;
    } else {
        if (x < damage_x0) damage_x0 = x;
        if (y < damage_y0) damage_y0 = y;
        if (x + w > damage_x1) damage_x1 = x + w;
        if (y + h > damage_y1) damage_y1 = y + h;
    }
    ui_dirty |= 4;
}

/* ------------------------------------------------------------ relogio */
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static int bcd(int v) { return (v & 15) + (v >> 4) * 10; }

static void rtc_read(void)
{
    int guard = 0;
    while ((cmos(0x0A) & 0x80) && guard++ < 100000) ;
    u8 b = cmos(0x0B);
    int h = cmos(0x04), m = cmos(0x02), d = cmos(0x07), mo = cmos(0x08);
    int pm = (!(b & 2)) && (h & 0x80);
    h &= 0x7F;
    if (!(b & 4)) { h = bcd(h); m = bcd(m); d = bcd(d); mo = bcd(mo); }
    if (!(b & 2)) { if (h == 12) h = 0; if (pm) h += 12; }
    clk_h = h; clk_m = m; clk_d = d; clk_mo = mo;
}

void ui_tick(void)
{
    int old = clk_m;
    rtc_read();
    if (clk_m != old) ui_dirty = 1;
    u32 fps = rendered_frames - frames_previous;
    frames_previous = rendered_frames;
    if (fps) frames_per_second = fps;      /* guarda o ultimo FPS medido em atividade */
    if (drag < 0 && ((wins[APP_SETTINGS].open && !wins[APP_SETTINGS].min) ||
        (wins[APP_NET].open && !wins[APP_NET].min))) ui_dirty = 1;
}

/* ------------------------------------------------------------ janelas */
static void zremove(int a)
{
    int k;
    for (k = 0; k < zn; k++) if (zorder[k] == a) break;
    if (k == zn) return;
    for (; k < zn - 1; k++) zorder[k] = zorder[k + 1];
    zn--;
}
static void focus(int a) { zremove(a); zorder[zn++] = a; }
static void open_app(int a) { wins[a].open = 1; wins[a].min = 0; focus(a); }
static void close_app(int a) { wins[a].open = 0; wins[a].min = 0; zremove(a); }
static int top_app(void)
{
    for (int k = zn - 1; k >= 0; k--) {
        int a = zorder[k];
        if (wins[a].open && !wins[a].min) return a;
    }
    return -1;
}
static void toggle_zoom(int a)
{
    Win *w = &wins[a];
    if (!w->zoom) {
        w->sx = w->x; w->sy = w->y; w->sw = w->w; w->sh = w->h;
        w->x = 16; w->y = MENUH + 8; w->w = SW - 32; w->h = SH - MENUH - 8 - 110;
        w->zoom = 1;
    } else {
        w->x = w->sx; w->y = w->sy; w->w = w->sw; w->h = w->sh;
        w->zoom = 0;
    }
}
static int win_at(int x, int y)
{
    for (int k = zn - 1; k >= 0; k--) {
        int a = zorder[k];
        Win *w = &wins[a];
        if (w->open && !w->min && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return a;
    }
    return -1;
}

/* ------------------------------------------------------------ terminal */
#define TL 48
#define TC 100
static char tbuf[TL][TC];
static int tn, tcol;
static char tin[TC];
static int tlen;

static void term_nl(void)
{
    tbuf[tn][tcol] = 0;
    tcol = 0;
    if (tn < TL - 1) tn++;
    else memmove(tbuf[0], tbuf[1], (TL - 1) * TC);
    tbuf[tn][0] = 0;
}
static void term_putc(char c)
{
    if (c == '\n') { term_nl(); return; }
    if (tcol >= TC - 1) term_nl();
    tbuf[tn][tcol++] = c;
    tbuf[tn][tcol] = 0;
}
void term_puts(const char *s) { while (*s) term_putc(*s++); }

static void reboot(void)
{
    for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------ tcptest */
static int term_parse_ip(const char *s, int len, u32 *out)
{
    u32 ip = 0;
    int part = 0, value = 0, digits = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            if (++digits > 3) return 0;
            value = value * 10 + (c - '0');
            if (value > 255) return 0;
        } else if (c == '.') {
            if (!digits || part == 3) return 0;
            ip = (ip << 8) | (u32)value;
            part++; digits = 0; value = 0;
        } else {
            return 0;
        }
    }
    if (part != 3 || !digits) return 0;
    *out = (ip << 8) | (u32)value;
    return 1;
}

static int term_parse_port(const char *s, int len, u16 *out)
{
    int value = 0, digits = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        value = value * 10 + (s[i] - '0');
        if (value > 65535) return 0;
        digits++;
    }
    if (!digits || !value) return 0;
    *out = (u16)value;
    return 1;
}

/* Blocking (single-threaded) HTTP probe: poll the NIC while waiting, with a
 * hard attempt/tick limit so a dead host cannot freeze the desktop forever. */
static void term_tcptest(const char *args)
{
    const char *ip_s = args, *ip_e, *port_s, *port_e, *tail;
    u32 ip;
    u16 port;
    char text[24], num[8];

    while (*ip_s == ' ') ip_s++;
    ip_e = ip_s;
    while (*ip_e && *ip_e != ' ') ip_e++;
    port_s = ip_e;
    while (*port_s == ' ') port_s++;
    port_e = port_s;
    while (*port_e && *port_e != ' ') port_e++;
    tail = port_e;
    while (*tail == ' ') tail++;

    if (ip_s == ip_e || port_s == port_e || *tail ||
        !term_parse_ip(ip_s, (int)(ip_e - ip_s), &ip) ||
        !term_parse_port(port_s, (int)(port_e - port_s), &port)) {
        term_puts("uso: tcptest <ip> <porta>   (ex: tcptest 10.0.2.2 8000)\n");
        return;
    }

    if (!net.present) { term_puts("tcptest: sem placa de rede\n"); return; }
    if (!net.link) { term_puts("tcptest: sem link de rede\n"); return; }

    net_ip_string(ip, text);
    itoa_(port, num);
    term_puts("tcptest: conectando a "); term_puts(text); term_putc(':');
    term_puts(num); term_puts(" ...\n");

    u32 start = timer_ticks, rx_before = net.rx_packets;
    TcpConn *conn = tcp_connect(ip, port, 0);
    if (!conn) {
        term_puts("tcptest: nao foi possivel abrir a conexao (sem slot livre)\n");
        return;
    }

    int attempts = 0, established = 0;
    while (attempts++ < 2000000) {
        if (tcp_state(conn) == TCP_ESTABLISHED) { established = 1; break; }
        if (tcp_state(conn) != TCP_SYN_SENT) break;
        net_poll();                       /* also drives tcp_poll() */
        if (!net.present || !net.link) break;
        if (timer_ticks - start >= TICK_HZ * 4) break;
    }

    int state = tcp_state(conn);
    if (!established || state != TCP_ESTABLISHED) {
        u32 elapsed = timer_ticks - start;
        int had_rx = net.rx_packets != rx_before;
        tcp_close(conn);
        if (!net.present) term_puts("tcptest: placa de rede parou de responder\n");
        else if (!net.link) term_puts("tcptest: sem link de rede\n");
        else if (state == TCP_CLOSED && elapsed < TICK_HZ * 5 / 2 && had_rx)
            term_puts("tcptest: conexao recusada (RST do servidor)\n");
        else if (!had_rx)
            term_puts("tcptest: sem ARP/SYN-ACK - destino inalcancavel na rede\n");
        else
            term_puts("tcptest: timeout ao conectar (sem SYN-ACK)\n");
        return;
    }

    static const char request[] = "GET / HTTP/1.0\r\n\r\n";
    term_puts("tcptest: conectado, enviando GET ...\n");
    if (tcp_send(conn, (const u8 *)request, (int)sizeof(request) - 1) <= 0) {
        term_puts("tcptest: falha ao enviar a requisicao\n");
        tcp_close(conn);
        return;
    }

    term_puts("tcptest: resposta:\n");
    u8 chunk[128];
    int received = 0, closed = 0, reset = 0, lost = 0;
    u32 deadline = timer_ticks + TICK_HZ * 5;
    attempts = 0;
    while (received < 500 && attempts++ < 2000000) {
        net_poll();
        int n = tcp_recv(conn, chunk, (int)sizeof(chunk));
        for (int i = 0; i < n && received < 500; i++, received++) {
            u8 b = chunk[i];
            if (b == '\r') continue;
            if (b == '\n' || b == '\t') term_putc(b == '\t' ? ' ' : '\n');
            else term_putc((b >= 32 && b < 127) ? (char)b : '.');
        }
        if (received >= 500) break;
        if (!net.present || !net.link) { lost = 1; break; }
        state = tcp_state(conn);
        if (state == TCP_CLOSED) { reset = 1; break; }
        if (n == 0 && (state == TCP_CLOSE_WAIT || state == TCP_CLOSING ||
                       state == TCP_LAST_ACK || state == TCP_TIME_WAIT)) {
            closed = 1;
            break;
        }
        if ((int)(timer_ticks - deadline) >= 0) break;
    }
    term_putc('\n');
    tcp_close(conn);

    if (!received) {
        if (lost) term_puts("tcptest: link caiu durante a resposta\n");
        else if (reset) term_puts("tcptest: conexao recusada (RST do servidor)\n");
        else if (closed) term_puts("tcptest: conexao encerrada sem resposta\n");
        else term_puts("tcptest: timeout aguardando a resposta HTTP\n");
    } else {
        term_puts("tcptest: ");
        itoa_(received, num);
        term_puts(num);
        term_puts(" bytes recebidos; conexao encerrada\n");
    }
}

/* Blocking (single-threaded) GET: poll the NIC and the HTTP state machine
 * until the callback fires or the global deadline expires. */
#define GET_BODY_LIMIT (4 * 1024)

static void term_get_done(int status, const u8 *body, int len, void *arg)
{
    (void)arg;
    char num[12];
    if (status <= 0) {
        term_puts("get: falhou: "); term_puts(http_last_error()); term_putc('\n');
        return;
    }
    term_puts("get: HTTP "); itoa_(status, num); term_puts(num);
    term_puts(", "); itoa_(len, num); term_puts(num); term_puts(" bytes\n");
    int shown = len > GET_BODY_LIMIT ? GET_BODY_LIMIT : len;
    for (int i = 0; i < shown; i++) {
        u8 b = body[i];
        if (b == '\r') continue;
        if (b == '\n' || b == '\t') term_putc(b == '\t' ? ' ' : '\n');
        else term_putc((b >= 32 && b < 127) ? (char)b : '.');
    }
    if (shown > 0 && body[shown - 1] != '\n') term_putc('\n');
    if (len > shown) {
        term_puts("(truncado, "); itoa_(len, num); term_puts(num);
        term_puts(" bytes no total)\n");
    }
}

static void term_get(const char *args)
{
    while (*args == ' ') args++;
    if (!*args) {
        term_puts("uso: get <url>   (ex: get http://10.0.2.2:8000/)\n");
        return;
    }
    if (http_busy()) {
        term_puts("get: ja existe uma requisicao em andamento\n");
        return;
    }
    term_puts("get: "); term_puts(args); term_putc('\n');
    if (!http_get(args, term_get_done, 0)) {
        term_puts("get: "); term_puts(http_last_error()); term_putc('\n');
        return;
    }
    u32 start = timer_ticks;
    int attempts = 0;
    while (http_busy() && attempts++ < 4000000) {
        net_poll();
        http_poll();
        if (timer_ticks - start >= TICK_HZ * 20) {
            http_cancel();
            term_puts("get: timeout\n");
            return;
        }
    }
    if (http_busy()) {
        http_cancel();
        term_puts("get: timeout\n");
    }
}

static void term_exec(char *cmd)
{
    term_puts("nimbus$ "); term_puts(cmd); term_putc('\n');
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;
    if (streq(cmd, "help")) {
        term_puts("comandos: help clear echo time\n");
        term_puts("  sysinfo apps reboot\n");
        term_puts("  open term|notes|calc|about|settings|net\n");
        term_puts("  ping  dns <dominio>  netinfo memtest\n");
        term_puts("  tcptest <ip> <porta>  (ex: tcptest 10.0.2.2 8000)\n");
        term_puts("  get <url>  (ex: get http://10.0.2.2:8000/)\n");
    } else if (streq(cmd, "tcptest") || starts(cmd, "tcptest ")) {
        term_tcptest(cmd + 7);
    } else if (streq(cmd, "get") || starts(cmd, "get ")) {
        term_get(cmd + 3);
    } else if (streq(cmd, "clear")) {
        tn = 0; tcol = 0; tbuf[0][0] = 0;
    } else if (starts(cmd, "echo ")) {
        term_puts(cmd + 5); term_putc('\n');
    } else if (streq(cmd, "time")) {
        char b[24] = "00:00  00/00\n"; b[12] = '\n'; b[13] = 0;
        put2(b, clk_h); put2(b + 3, clk_m); put2(b + 7, clk_d); put2(b + 10, clk_mo);
        term_puts(b);
    } else if (streq(cmd, "sysinfo")) {
        char n[16];
        term_puts("Nimbus OS 0.1 (kernel proprio, x86)\n");
        term_puts("video: "); itoa_(SW, n); term_puts(n); term_puts("x");
        itoa_(SH, n); term_puts(n); term_puts(" 32bpp\n");
    } else if (streq(cmd, "apps")) {
        term_puts("terminal notas calculadora sobre ajustes rede\n");
    } else if (streq(cmd, "ping")) {
        net_ping(); term_puts("Veja o resultado no app Rede.\n"); open_app(APP_NET);
    } else if (starts(cmd, "dns ")) {
        net_resolve(cmd + 4); open_app(APP_NET);
    } else if (streq(cmd, "netinfo")) {
        char ip[16]; net_ip_string(net.ip, ip);
        term_puts(net.status); term_putc('\n'); term_puts(ip); term_putc('\n');
    } else if (starts(cmd, "open ")) {
        const char *a = cmd + 5;
        if (streq(a, "term")) open_app(APP_TERM);
        else if (streq(a, "notes")) open_app(APP_NOTES);
        else if (streq(a, "calc")) open_app(APP_CALC);
        else if (streq(a, "about")) open_app(APP_ABOUT);
        else if (streq(a, "settings")) open_app(APP_SETTINGS);
        else if (streq(a, "net")) open_app(APP_NET);
        else term_puts("app desconhecido\n");
    } else if (streq(cmd, "reboot")) {
        reboot();
    } else {
        term_puts("comando nao encontrado: "); term_puts(cmd); term_putc('\n');
    }
}

static void term_key(char c)
{
    if (c == '\n') { tin[tlen] = 0; term_exec(tin); tlen = 0; }
    else if (c == '\b') { if (tlen) tlen--; }
    else if (c >= 32 && c < 127 && tlen < 60) tin[tlen++] = c;
}

static void draw_term(Win *w)
{
    int cx = w->x + 12, cy = w->y + TITLEH + 10;
    int cols = (w->w - 24) / CW;
    int rows = (w->h - TITLEH - 16) / LH;
    int first = tn - (rows - 1);
    if (first < 0) first = 0;
    int y = cy;
    for (int i = first; i < tn; i++, y += LH)
        draw_text_n(cx, y, tbuf[i], 0xCFE8CF, TS, cols);
    draw_text(cx, y, "nimbus$ ", 0x7CFC9A, TS);
    tin[tlen] = 0;
    draw_text_n(cx + 8 * CW, y, tin, WHITE, TS, cols - 9);
    fill_rect(cx + (8 + tlen) * CW, y, 10, 14, 0xCFE8CF);
}

/* ------------------------------------------------------------ notas */
static char nbuf[2048];
static int nlen;

static void notes_key(char c)
{
    if (c == '\b') { if (nlen) nlen--; }
    else if ((c == '\n' || (c >= 32 && c < 127)) && nlen < (int)sizeof(nbuf) - 1) nbuf[nlen++] = c;
}

static void draw_notes(Win *w)
{
    fill_rect(w->x + 1, w->y + TITLEH, 146, w->h - TITLEH - 12, 0xF1F0ED);
    ui_text(w->x + 17, w->y + TITLEH + 12, "MINHAS NOTAS", 0x8B8985);
    fill_round(w->x + 9, w->y + TITLEH + 46, 128, 56, 7, 7, 0xE7DEBF, 255);
    ui_text(w->x + 18, w->y + TITLEH + 51, "Boas-vindas", 0x36332B);
    ui_text(w->x + 18, w->y + TITLEH + 73, "Em memoria", 0x777267);
    fill_rect(w->x + 147, w->y + TITLEH, 1, w->h - TITLEH - 10, 0xE4E2DD);
    ui_text(w->x + 168, w->y + TITLEH + 12, "Seu espaco para ideias", 0x8B8985);
    int cx = w->x + 168, cy = w->y + TITLEH + 48;
    int px = cx, y = cy;
    for (int i = 0; i <= nlen; i++) {
        if (y + 22 > w->y + w->h - 12) break;
        if (i == nlen) { fill_rect(px, y + 4, 2, 17, 0xD9A523); break; }
        char c = nbuf[i];
        if (c == '\n') { px = cx; y += 25; continue; }
        char text[2] = { c, 0 };
        int advance = ui_text_width(text);
        if (px + advance > w->x + w->w - 20) { px = cx; y += 25; }
        if (y + 22 > w->y + w->h - 12) break;
        ui_text(px, y, text, 0x353942);
        px += advance;
    }
}

/* ------------------------------------------------------------ calculadora */
static const char *calc_lbl[16] = { "7","8","9","/", "4","5","6","*", "1","2","3","-", "0","C","=","+" };
static int cacc, ccur, cnew = 1, cerr;
static char cop;

static void calc_apply(void)
{
    switch (cop) {
    case '+': cacc += ccur; break;
    case '-': cacc -= ccur; break;
    case '*': cacc *= ccur; break;
    case '/': if (ccur == 0) { cerr = 1; cacc = 0; } else cacc /= ccur; break;
    default: cacc = ccur;
    }
    ccur = cacc;
}

static void calc_key(char k)
{
    if (cerr && k != 'C') return;
    if (k >= '0' && k <= '9') {
        if (cnew) { ccur = 0; cnew = 0; }
        if (ccur < 100000000) ccur = ccur * 10 + (k - '0');
    } else if (k == 'C') {
        cacc = 0; ccur = 0; cop = 0; cnew = 1; cerr = 0;
    } else if (k == '=') {
        if (cop) { calc_apply(); cop = 0; }
        cnew = 1;
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (cop && !cnew) calc_apply(); else cacc = ccur;
        cop = k; cnew = 1;
    }
}

static void calc_geom(Win *w, int i, int *bx, int *by, int *bw, int *bh)
{
    int gap = 8;
    int cw = (w->w - 20 - 3 * gap) / 4;
    int ch = (w->h - TITLEH - 76 - 10 - 3 * gap) / 4;
    *bx = w->x + 10 + (i % 4) * (cw + gap);
    *by = w->y + TITLEH + 76 + (i / 4) * (ch + gap);
    *bw = cw; *bh = ch;
}

static void draw_calc(Win *w)
{
    fill_round(w->x + 10, w->y + TITLEH + 10, w->w - 20, 56, 10, 10, 0x1F1F26, 255);
    char buf[16];
    if (cerr) { buf[0] = 'E'; buf[1] = 'r'; buf[2] = 'r'; buf[3] = 'o'; buf[4] = 0; }
    else itoa_(ccur, buf);
    int tw = slen(buf) * 18;
    draw_text(w->x + w->w - 24 - tw, w->y + TITLEH + 10 + 17, buf, WHITE, 3);
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        calc_geom(w, i, &bx, &by, &bw, &bh);
        char c = calc_lbl[i][0];
        int op = (c == '/' || c == '*' || c == '-' || c == '+' || c == '=');
        u32 col = op ? 0xFF9F0A : (c == 'C' ? 0xA5A5AD : 0x56565E);
        fill_round(bx, by, bw, bh, 10, 10, col, 255);
        draw_char(bx + (bw - 18) / 2, by + (bh - 21) / 2, c, WHITE, 3);
    }
}

static void calc_click(int x, int y)
{
    Win *w = &wins[APP_CALC];
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        calc_geom(w, i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) { calc_key(calc_lbl[i][0]); return; }
    }
}

/* ------------------------------------------------------------ sobre */
static void draw_about(Win *w)
{
    int cx = w->x + 24, cy = w->y + TITLEH + 22;
    fill_circle(cx + 14, cy + 22, 14, 0x4A90E2);
    fill_circle(cx + 34, cy + 12, 18, 0x4A90E2);
    fill_circle(cx + 56, cy + 22, 14, 0x4A90E2);
    fill_rect(cx + 14, cy + 22, 42, 14, 0x4A90E2);
    ui_text(cx + 90, cy + 4, "Nimbus OS", 0x1C1C20);
    ui_text(cx + 90, cy + 28, "Um desktop feito para voce", 0x6A6A72);
    char n[16], l[48] = "Tela: ";
    int k = 6;
    itoa_(SW, n); for (int i = 0; n[i]; i++) l[k++] = n[i];
    l[k++] = 'x';
    itoa_(SH, n); for (int i = 0; n[i]; i++) l[k++] = n[i];
    l[k] = 0;
    ui_text(cx, cy + 64, "Ctrl + Espaco   Buscar aplicativos", 0x2A2A2E);
    ui_text(cx, cy + 88, "Alt + Tab   Alternar janelas", 0x2A2A2E);
    ui_text(cx, cy + 112, "Alt + F4   Fechar     Ctrl + M   Minimizar", 0x2A2A2E);
    ui_text(cx, cy + 144, l, 0x6A6A72);
}

/* ------------------------------------------------------------ desenho de janela */
static void text_number(int x, int y, const char *label, u32 value)
{
    char number[16]; itoa_((int)value, number);
    ui_text(x, y, label, 0x777E8C);
    ui_text(x + ui_text_width(label) + 8, y, number, 0x283144);
}

static void button_draw(int x, int y, int w, const char *label, int selected)
{
    fill_round(x, y, w, 34, 7, 7, selected ? accent : 0xE8EBF2, 255);
    ui_text(x + (w - ui_text_width(label)) / 2, y + 6, label, selected ? WHITE : 0x323B4C);
}

static void draw_settings(Win *w)
{
    int x = w->x + 24, y = w->y + TITLEH + 14;
    ui_text(x, y, "Personalize seu Nimbus", 0x283144);
    ui_text(x, y + 30, "PAPEL DE PAREDE", 0x888E9A);
    static const u32 swatches[3] = {0x7273B5, 0x3B8E96, 0xC67C67};
    static const char *names[3] = {"Aurora", "Oceano", "Entardecer"};
    for (int i = 0; i < 3; i++) {
        int bx = x + i * 160;
        fill_round(bx - 2, y + 58, 148, 72, 10, 10, appearance_palette == i ? accent : 0xE1E5ED, 255);
        fill_round(bx, y + 60, 144, 68, 8, 8, swatches[i], 255);
        ui_text(bx + 12, y + 83, names[i], WHITE);
    }
    ui_text(x, y + 146, "COR DE DESTAQUE", 0x888E9A);
    static const u32 colors[4] = {0x3478E5, 0x8B5DD7, 0x218A71, 0xC65F77};
    for (int i = 0; i < 4; i++) {
        if (accent == colors[i]) fill_circle(x + 16 + i * 48, y + 194, 18, 0xB8C1D2);
        fill_circle(x + 16 + i * 48, y + 194, 14, colors[i]);
    }
    ui_text(x, y + 228, "Animacao do dock", 0x283144);
    button_draw(x + 300, y + 222, 176, dock_animation ? "Ativada" : "Desativada", dock_animation);
    fill_rect(x, y + 276, w->w - 48, 1, 0xE1E5ED);
    text_number(x, y + 288, "Ultimo FPS medido:", frames_per_second);
    text_number(x, y + 314, "Tempo medio de desenho (ms):", rendered_frames ? render_ticks * 1000 / TICK_HZ / rendered_frames : 0);
    ui_text(x, y + 344, "Mouse 200 Hz  /  entrada 240 Hz  /  cache de arraste", 0x888E9A);
}

static void settings_click(int px, int py)
{
    Win *w = &wins[APP_SETTINGS];
    int x = px - w->x - 24, y = py - w->y - TITLEH - 14;
    if (y >= 58 && y < 130 && x >= 0 && x < 480 && x % 160 < 146) {
        appearance_palette = x / 160; make_wallpaper();
    }
    if (y >= 174 && y <= 214 && x >= 0 && x < 192) {
        static const u32 colors[4] = {0x3478E5, 0x8B5DD7, 0x218A71, 0xC65F77};
        accent = colors[x / 48];
    }
    if (y >= 222 && y < 256 && x >= 300 && x < 476) dock_animation = !dock_animation;
}

static void draw_network(Win *w)
{
    int x = w->x + 24, y = w->y + TITLEH + 14;
    fill_round(x, y, w->w - 48, 68, 10, 10, 0xEDF1F7, 255);
    fill_circle(x + 22, y + 25, 6, net.link ? 0x36B889 : 0xCF7780);
    ui_text(x + 40, y + 10, net.present ? "Ethernet - Realtek RTL8139" : "Ethernet indisponivel", 0x283144);
    ui_text(x + 16, y + 36, net.status, 0x677183);
    char ip[16];
    ui_text(x, y + 84, "Endereco IPv4", 0x888E9A); net_ip_string(net.ip, ip); ui_text(x + 160, y + 84, ip, 0x283144);
    ui_text(x, y + 110, "Gateway", 0x888E9A); net_ip_string(net.gateway, ip); ui_text(x + 160, y + 110, ip, 0x283144);
    ui_text(x, y + 136, "Servidor DNS", 0x888E9A); net_ip_string(net.dns, ip); ui_text(x + 160, y + 136, ip, 0x283144);
    button_draw(x + 346, y + 94, 160, "Testar gateway", 0);
    fill_round(x, y + 176, 340, 36, 7, 7, 0xE4E9F1, 255);
    fill_round(x + 1, y + 177, 338, 34, 6, 6, WHITE, 255);
    const char *domain = network_domain + (network_domain_len > 36 ? network_domain_len - 36 : 0);
    ui_text(x + 10, y + 183, domain, 0x283144);
    button_draw(x + 354, y + 177, 152, net.busy ? "Aguarde..." : "Consultar DNS", 1);
    if (net.resolved) {
        net_ip_string(net.resolved, ip);
        ui_text(x, y + 228, "IPv4 encontrado:", 0x888E9A); ui_text(x + 140, y + 228, ip, accent);
    } else {
        ui_text(x, y + 228, "Ultimo ping (ms):", 0x888E9A);
        char ms[8];
        if (net.ping_ms) itoa_(net.ping_ms, ms); else { ms[0] = '<'; ms[1] = '4'; ms[2] = 0; }
        ui_text(x + ui_text_width("Ultimo ping (ms):") + 8, y + 228, ms, 0x283144);
    }
    text_number(x, y + 270, "TX", net.tx_packets);
    text_number(x + 170, y + 270, "RX", net.rx_packets);
    text_number(x + 340, y + 270, "Descartados", net.dropped);
    ui_text(x, y + 314, "Perfil estatico QEMU NAT. Digite um dominio e pressione Enter.", 0x888E9A);
}

static void network_click(int px, int py)
{
    Win *w = &wins[APP_NET];
    int x = px - w->x - 24, y = py - w->y - TITLEH - 14;
    if (x >= 346 && x < 506 && y >= 94 && y < 128) net_ping();
    if (x >= 354 && x < 506 && y >= 177 && y < 211) net_resolve(network_domain);
}

static void draw_window(int a, int active)
{
    Win *w = &wins[a];
    int dark = (a == APP_TERM);

    for (int e = 5; e >= 0; e--) {
        int g = 2 + e * 3;
        fill_round(w->x - g, w->y - g + 7, w->w + 2 * g, w->h + 2 * g, 12 + g, 12 + g, 0x10152C, active ? 11 : 6);
    }
    u32 body = dark ? 0x20232C : 0xFDFCF9;
    u32 tbar = dark ? (active ? 0x30343F : 0x292C35) : (active ? 0xF6F5F3 : 0xEEEDEA);
    fill_round(w->x - 1, w->y - 1, w->w + 2, w->h + 2, 13, 13, WHITE, 100);
    fill_round(w->x, w->y, w->w, w->h, 12, 12, body, 255);
    fill_round(w->x, w->y, w->w, TITLEH, 12, 0, tbar, 255);
    fill_rect(w->x, w->y + TITLEH - 1, w->w, 1, dark ? 0x101014 : 0xCFCFD4);

    u32 lights[3] = { 0xFF5F57, 0xFEBC2E, 0x28C840 };
    for (int i = 0; i < 3; i++) {
        u32 c = active ? lights[i] : (dark ? 0x55555C : 0xC4C4CA);
        fill_circle(w->x + 20 + i * 22, w->y + TITLEH / 2, 7, c);
    }
    int tw = ui_text_width(w->title);
    int tx = w->x + (w->w - tw) / 2;
    if (tx < w->x + 82) tx = w->x + 82;               /* nao encosta nos botoes coloridos */
    ui_text(tx, w->y + 6, w->title, dark ? 0xDDDDE4 : 0x3A3A40);

    switch (a) {
    case APP_TERM:  draw_term(w); break;
    case APP_NOTES: draw_notes(w); break;
    case APP_CALC:  draw_calc(w); break;
    case APP_ABOUT: draw_about(w); break;
    case APP_SETTINGS: draw_settings(w); break;
    case APP_NET: draw_network(w); break;
    }
}

/* ------------------------------------------------------------ dock */
#define DOCK_BASE 52
#define DOCK_GAP  14
static int dock_ix[NAPPS], dock_iy[NAPPS], dock_is[NAPPS];
static int dock_size[NAPPS] = {52, 52, 52, 52, 52, 52};

void ui_animate(void)
{
    if (last_net_revision != net.revision) {
        last_net_revision = net.revision;
        if (wins[APP_NET].open && !wins[APP_NET].min && drag < 0) ui_dirty = 1;
    }
    if (drag >= 0) return;
    int x0 = (SW - (NAPPS * DOCK_BASE + (NAPPS - 1) * DOCK_GAP)) / 2;
    for (int i = 0; i < NAPPS; i++) {
        int d = iabs(mx - (x0 + i * (DOCK_BASE + DOCK_GAP) + DOCK_BASE / 2));
        int target = DOCK_BASE + ((my > SH - 130 && d < 120) ? 32 * (120 - d) / 120 : 0);
        int delta = target - dock_size[i];
        if (delta) {
            if (dock_animation) dock_size[i] += delta > 0 ? (delta + 2) / 3 : (delta - 2) / 3;
            else dock_size[i] = target;
            ui_dirty |= 2;
        }
    }
}

static void dock_layout(void)
{
    int sizes[NAPPS], total = (NAPPS - 1) * DOCK_GAP;
    for (int i = 0; i < NAPPS; i++) {
        sizes[i] = dock_size[i];
        total += sizes[i];
    }
    int x = (SW - total) / 2;
    for (int i = 0; i < NAPPS; i++) {
        dock_ix[i] = x; dock_is[i] = sizes[i]; dock_iy[i] = SH - 16 - sizes[i];
        x += sizes[i] + DOCK_GAP;
    }
}

static void draw_icon(int app, int x, int y, int s)
{
    int r = s / 4;
    switch (app) {
    case APP_TERM: {
        fill_round(x, y, s, s, r, r, 0x23232B, 255);
        int u = s / 10, cx = x + s * 2 / 10, cy = y + s * 3 / 10;
        for (int k = 0; k < 4; k++) {
            fill_rect(cx + k * u, cy + k * u, u + 1, u + 1, 0x7CFC9A);
            fill_rect(cx + k * u, cy + 6 * u - k * u, u + 1, u + 1, 0x7CFC9A);
        }
        fill_rect(x + s * 5 / 10, y + s * 7 / 10, s * 3 / 10, u, 0x7CFC9A);
        break; }
    case APP_NOTES: {
        fill_round(x, y, s, s, r, r, 0xFFF7D1, 255);
        fill_round(x, y, s, s / 4, r, 0, 0xF5C542, 255);
        int t = s / 20 > 2 ? s / 20 : 2;
        for (int k = 0; k < 3; k++)
            fill_rect(x + s / 6, y + s * 2 / 5 + k * (s / 6), s * 2 / 3, t, 0x9A9A8A);
        break; }
    case APP_CALC: {
        fill_round(x, y, s, s, r, r, 0x3A3A44, 255);
        fill_rect(x + s / 8, y + s / 8, s * 3 / 4, s / 5, 0x9AE6B4);
        int g = s / 16 > 2 ? s / 16 : 2;
        int bw = (s * 3 / 4 - 2 * g) / 3, bh = (s / 2 - 2 * g) / 3;
        for (int rr = 0; rr < 3; rr++)
            for (int cc = 0; cc < 3; cc++)
                fill_round(x + s / 8 + cc * (bw + g), y + s * 2 / 5 + rr * (bh + g), bw, bh, 2, 2,
                           cc == 2 ? 0xFF9F0A : 0x8A8A96, 255);
        break; }
    case APP_ABOUT:
        fill_round(x, y, s, s, r, r, 0x2D7FF9, 255);
        fill_circle(x + s / 2, y + s * 3 / 10, s / 12 + 1, WHITE);
        fill_rect(x + s / 2 - s / 14, y + s * 45 / 100, s / 7, s * 35 / 100, WHITE);
        break;
    case APP_SETTINGS:
        fill_round(x, y, s, s, r, r, 0x68758A, 255);
        for (int i = 0; i < 3; i++) {
            int yy = y + s * (3 + i * 2) / 10;
            fill_rect(x + s / 5, yy, s * 3 / 5, 2, 0xE8EDF5);
            fill_circle(x + s * (i == 1 ? 6 : 4) / 10, yy, s / 12, WHITE);
        }
        break;
    case APP_NET:
        fill_round(x, y, s, s, r, r, 0x278DA3, 255);
        fill_circle(x + s / 2, y + s / 2, s * 3 / 10, 0xD7F3EF);
        fill_circle(x + s / 2, y + s / 2, s / 4, 0x278DA3);
        fill_rect(x + s / 2 - 1, y + s / 5, 2, s * 3 / 5, 0xD7F3EF);
        fill_rect(x + s / 5, y + s / 2 - 1, s * 3 / 5, 2, 0xD7F3EF);
        break;
    }
}

static void draw_dock(void)
{
    dock_layout();
    int px = dock_ix[0] - 14;
    int pw = dock_ix[NAPPS - 1] + dock_is[NAPPS - 1] - dock_ix[0] + 28;
    int ph = DOCK_BASE + 22, py = SH - ph - 6;
    fill_round(px - 5, py + 4, pw + 10, ph + 2, 22, 22, 0x10152C, 24);
    fill_round(px - 1, py - 1, pw + 2, ph + 2, 19, 19, WHITE, 140);
    fill_round(px, py, pw, ph, 18, 18, 0xD9E1F2, 110);
    for (int i = 0; i < NAPPS; i++) {
        fill_round(dock_ix[i] - 1, dock_iy[i] - 1, dock_is[i] + 2, dock_is[i] + 2, dock_is[i] / 4, dock_is[i] / 4, WHITE, 110);
        draw_icon(i, dock_ix[i], dock_iy[i], dock_is[i]);
        if (wins[i].open)
            fill_circle(dock_ix[i] + dock_is[i] / 2, SH - 10, 2, WHITE);
        if (mx >= dock_ix[i] && mx < dock_ix[i] + dock_is[i] && my >= dock_iy[i] && my < SH - 12) {
            int tw = ui_text_width(wins[i].title), tx = dock_ix[i] + (dock_is[i] - tw) / 2;
            fill_round(tx - 12, dock_iy[i] - 38, tw + 24, 28, 7, 7, 0xF3F4F8, 245);
            ui_text(tx, dock_iy[i] - 35, wins[i].title, 0x2A3040);
        }
    }
}

/* ------------------------------------------------------------ barra de menu */
static int menu_left[4], menu_right[4];
static const char *menu_items[4][4] = {
    { "Sobre o Nimbus", "Buscar aplicativos", "Mostrar mesa", "Reiniciar" },
    { "Abrir Terminal", "Abrir Notas", "Abrir Calculadora", "Fechar janela" },
    { "Minimizar", "Ampliar / restaurar", "Proxima janela", "Mostrar mesa" },
    { "Atalhos de teclado", "Sobre o Nimbus", 0, 0 }
};

static void show_desktop(void)
{
    for (int a = 0; a < NAPPS; a++) if (wins[a].open) wins[a].min = 1;
}

static int search_match(int a)
{
    const char *name = wins[a].title;
    for (int start = 0; name[start]; start++) {
        int i = 0;
        while (search_query[i] && name[start + i]) {
            char c = name[start + i], q = search_query[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (q >= 'A' && q <= 'Z') q += 32;
            if (c != q) break;
            i++;
        }
        if (!search_query[i]) return 1;
    }
    return 0;
}

static int search_app(int row)
{
    for (int a = 0; a < NAPPS; a++) if (search_match(a) && row-- == 0) return a;
    return -1;
}

void ui_shortcut(int key)
{
    int top = top_app();
    if (key == 1) {
        search_open = !search_open;
        search_len = search_selected = 0; search_query[0] = 0;
    } else if (key == 2) {
        for (int k = 0; k < zn; k++) {
            int a = zorder[k];
            if (a != top && !wins[a].min) { focus(a); break; }
        }
    } else if (key == 3 && top >= 0) close_app(top);
    else if (key == 4 && top >= 0) wins[top].min = 1;
    else if (key == 5 && search_open && search_selected > 0) search_selected--;
    else if (key == 6 && search_open && search_app(search_selected + 1) >= 0) search_selected++;
    menu = -1;
    drag = -1;
    ui_dirty = 1;
}

static void menu_action(int row)
{
    int m = menu, top = top_app();
    menu = -1;
    if (m == 0) {
        if (row == 0) open_app(APP_ABOUT);
        if (row == 1) ui_shortcut(1);
        if (row == 2) show_desktop();
        if (row == 3) reboot();
    } else if (m == 1) {
        if (row < 3) open_app(row);
        else if (top >= 0) close_app(top);
    } else if (m == 2) {
        if (row == 0 && top >= 0) wins[top].min = 1;
        if (row == 1 && top >= 0) toggle_zoom(top);
        if (row == 2) ui_shortcut(2);
        if (row == 3) show_desktop();
    } else if (m == 3) open_app(APP_ABOUT);
}

static void draw_menubar(int top)
{
    fill_rect_a(0, 0, SW, MENUH, 0xDCE5F5, 50);
    fill_rect_a(0, MENUH - 1, SW, 1, WHITE, 35);
    fill_circle(17, 17, 5, WHITE);
    fill_circle(24, 13, 7, WHITE);
    fill_circle(32, 17, 5, WHITE);
    fill_rect(17, 17, 16, 5, WHITE);

    const char *name = top >= 0 ? wins[top].title : "Nimbus";
    ui_text(52, 4, name, WHITE);
    menu_left[0] = 0; menu_right[0] = 44;
    int x = 52 + ui_text_width(name) + 28;
    static const char *items[] = { "Arquivo", "Janela", "Ajuda" };
    for (int i = 0; i < 3; i++) {
        int width = ui_text_width(items[i]);
        menu_left[i + 1] = x - 10; menu_right[i + 1] = x + width + 10;
        if (menu == i + 1) fill_round(x - 10, 2, width + 20, 26, 5, 5, WHITE, 45);
        ui_text(x, 4, items[i], WHITE);
        x += width + 30;
    }
    char t[16] = "00/00  00:00";
    put2(t, clk_d); put2(t + 3, clk_mo); put2(t + 7, clk_h); put2(t + 10, clk_m);
    ui_text(SW - 16 - ui_text_width(t), 4, t, WHITE);
    fill_circle(SW - 166, 13, 5, WHITE);
    fill_circle(SW - 166, 13, 3, 0x5D6E9C);
    for (int i = 0; i < 5; i++) fill_rect(SW - 162 + i, 17 + i, 2, 2, WHITE);
}

static void draw_overlays(void)
{
    if (menu >= 0) {
        int rows = menu == 3 ? 2 : 4;
        fill_round(menu_x - 4, MENUH + 6, 232, rows * 32 + 16, 12, 12, 0, 30);
        fill_round(menu_x, MENUH + 2, 224, rows * 32 + 12, 9, 9, 0xF5F6FA, 255);
        for (int i = 0; i < rows; i++) {
            if (i == menu_hover) fill_round(menu_x + 5, MENUH + 8 + i * 32, 214, 30, 5, 5, accent, 255);
            ui_text(menu_x + 15, MENUH + 11 + i * 32, menu_items[menu][i], i == menu_hover ? WHITE : 0x252A35);
        }
    }
    if (search_open) {
        int x = (SW - 520) / 2, y = 110;
        fill_round(x - 10, y - 3, 540, 366, 22, 22, 0, 35);
        fill_round(x, y, 520, 352, 15, 15, 0xF5F6FA, 255);
        ui_text(x + 24, y + 17, search_len ? search_query : "Buscar aplicativos...", search_len ? 0x222A38 : 0x818794);
        ui_text(x + 460, y + 17, "ESC", 0x818794);
        fill_rect(x + 20, y + 52, 480, 1, 0xDDE0E7);
        if (search_app(0) < 0) ui_text(x + 24, y + 78, "Nenhum aplicativo encontrado", 0x818794);
        for (int row = 0; row < NAPPS; row++) {
            int a = search_app(row);
            if (a < 0) break;
            int yy = y + 64 + row * 42;
            if (row == search_selected) fill_round(x + 12, yy, 496, 40, 7, 7, accent, 255);
            draw_icon(a, x + 24, yy + 5, 30);
            ui_text(x + 68, yy + 9, wins[a].title, row == search_selected ? WHITE : 0x252A35);
        }
        ui_text(x + 24, y + 324, "Enter para abrir  /  Setas para escolher", 0x818794);
    }
}

/* ------------------------------------------------------------ render geral */
static void cache_drag(void)
{
    gfx_clip(0, 0, SW, SH);
    memcpy32(scene, wallpaper, SW * SH);
    for (int k = 0; k < zn; k++) {
        int a = zorder[k];
        if (a != drag && wins[a].open && !wins[a].min) draw_window(a, 0);
    }
    memcpy32(drag_background, scene, SW * SH);
    u32 *saved_scene = scene;
    scene = drag_surface;
    memset(scene, 0, SW * SH * 4);
    Win *w = &wins[drag];
    int x = w->x, y = w->y;
    w->x = 20; w->y = 20;
    gfx_layer_mode(1);
    draw_window(drag, 1);
    gfx_layer_mode(0);
    w->x = x; w->y = y;
    drag_surface_w = w->w + 40;
    drag_surface_h = w->h + 48;
    if (drag_surface_w > SW) drag_surface_w = SW;
    if (drag_surface_h > SH) drag_surface_h = SH;
    scene = saved_scene;
    drag_cached = 1;
}

void ui_render(void)
{
    if (drag >= 0 && (!drag_cached || (ui_dirty & 1))) cache_drag();
    int x = 0, y = 0, right = SW, bottom = SH;
    if (!(ui_dirty & 1)) {
        if (ui_dirty & 4) {
            x = damage_x0; y = damage_y0; right = damage_x1; bottom = damage_y1;
            if (ui_dirty & 2) { x = 0; right = SW; bottom = SH; if (y > SH - 160) y = SH - 160; }
        } else y = SH - 160;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > SW) right = SW;
    if (bottom > SH) bottom = SH;
    gfx_clip(x, y, right - x, bottom - y);
    if (drag >= 0 && drag_cached) {
        for (int row = y; row < bottom; row++)
            memcpy32(scene + row * SW + x, drag_background + row * SW + x, right - x);
        gfx_blit_layer(drag_surface, wins[drag].x - 20, wins[drag].y - 20, drag_surface_w, drag_surface_h);
        /* O dock e a barra sempre ficam acima das janelas. */
        draw_dock();
        draw_menubar(drag);
        return;
    }
    for (int row = y; row < bottom; row++)
        memcpy32(scene + row * SW + x, wallpaper + row * SW + x, right - x);
    int top = top_app();
    for (int k = 0; k < zn; k++) {
        int a = zorder[k];
        Win *w = &wins[a];
        if (w->open && !w->min && w->x - 20 < right && w->x + w->w + 20 > x &&
            w->y - 20 < bottom && w->y + w->h + 28 > y) draw_window(a, a == top);
    }
    draw_dock();
    draw_menubar(top);
    draw_overlays();
}

/* ------------------------------------------------------------ eventos */
static void click(int x, int y)
{
    ui_dirty = 1;
    if (search_open) {
        int sx = (SW - 520) / 2;
        if (x >= sx && x < sx + 520 && y >= 174 && y < 426) {
            int a = search_app((y - 174) / 42);
            if (a >= 0) { open_app(a); search_open = 0; }
        } else if (x < sx || x >= sx + 520 || y < 110 || y >= 462 || (x > sx + 450 && y < 162)) search_open = 0;
        return;
    }
    if (y < MENUH) {
        if (x > SW - 182 && x < SW - 145) { ui_shortcut(1); return; }
        for (int i = 0; i < 4; i++) if (x >= menu_left[i] && x < menu_right[i]) {
            menu = menu == i ? -1 : i;
            menu_x = menu_left[i] < 8 ? 8 : menu_left[i];
            menu_hover = -1;
            return;
        }
        menu = -1;
        return;
    }
    if (menu >= 0) {
        int row = (y - MENUH - 8) / 32;
        if (x >= menu_x && x < menu_x + 224 && y >= MENUH + 8 && row < (menu == 3 ? 2 : 4)) menu_action(row);
        else menu = -1;
        return;
    }
    dock_layout();
    for (int i = 0; i < NAPPS; i++)
        if (x >= dock_ix[i] && x < dock_ix[i] + dock_is[i] && y >= dock_iy[i] && y < dock_iy[i] + dock_is[i]) {
            open_app(i);
            return;
        }
    int a = win_at(x, y);
    if (a < 0) return;
    focus(a);
    Win *w = &wins[a];
    int lx = x - w->x, ly = y - w->y;
    if (ly < TITLEH) {
        for (int i = 0; i < 3; i++) {
            int dx = lx - (20 + i * 22), dy = ly - TITLEH / 2;
            if (dx * dx + dy * dy <= 100) {
                if (i == 0) close_app(a);
                else if (i == 1) w->min = 1;
                else toggle_zoom(a);
                return;
            }
        }
        drag = a; dragdx = lx; dragdy = ly; drag_cached = 0;
        return;
    }
    if (a == APP_CALC) calc_click(x, y);
    if (a == APP_SETTINGS) settings_click(x, y);
    if (a == APP_NET) network_click(x, y);
}

void ui_on_mouse(int dx, int dy, int btn)
{
    int ox = mx, oy = my;
    mx += dx; my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > SW - 1) mx = SW - 1;
    if (my > SH - 1) my = SH - 1;
    int down = (btn & 1) && !(mbtn & 1);
    int up = !(btn & 1) && (mbtn & 1);
    mbtn = btn;

    if (drag >= 0) {
        if (btn & 1) {
            Win *w = &wins[drag];
            damage(w->x - 20, w->y - 20, w->w + 40, w->h + 48);
            w->x = mx - dragdx;
            w->y = my - dragdy;
            if (w->y < MENUH) w->y = MENUH;
            if (w->y > SH - 90) w->y = SH - 90;
            if (w->x < 80 - w->w) w->x = 80 - w->w;
            if (w->x > SW - 80) w->x = SW - 80;
            damage(w->x - 20, w->y - 20, w->w + 40, w->h + 48);
        } else { drag = -1; drag_cached = 0; ui_dirty = 1; }
    }
    if (up && drag >= 0) { drag = -1; drag_cached = 0; ui_dirty = 1; }
    if (down) click(mx, my);
    if (menu >= 0) {
        int hover = -1;
        if (mx >= menu_x && mx < menu_x + 224 && my >= MENUH + 8) {
            hover = (my - MENUH - 8) / 32;
            if (hover >= (menu == 3 ? 2 : 4)) hover = -1;
        }
        if (hover != menu_hover) { menu_hover = hover; ui_dirty = 1; }
    }

    int hot = my > SH - 130;
    if ((hot || dock_hot) && (mx != ox || my != oy)) ui_dirty |= 2;
    dock_hot = hot;
}

void ui_on_key(char ch)
{
    if (ch == 27) { search_open = 0; menu = -1; ui_dirty = 1; return; }
    if (search_open) {
        if (ch == '\n') {
            int a = search_app(search_selected);
            if (a >= 0) { open_app(a); search_open = 0; }
        } else {
            if (ch == '\b' && search_len) search_len--;
            else if (ch >= 32 && ch < 127 && search_len < 39) search_query[search_len++] = ch;
            search_query[search_len] = 0; search_selected = 0;
        }
        ui_dirty = 1;
        return;
    }
    if (menu >= 0) { menu = -1; ui_dirty = 1; return; }
    int t = top_app();
    if (t == APP_TERM) term_key(ch);
    else if (t == APP_NOTES) notes_key(ch);
    else if (t == APP_CALC) {
        if (ch == '\n') calc_key('=');
        else if (ch == '\b' || ch == 'c' || ch == 'C') calc_key('C');
        else calc_key(ch);
    } else if (t == APP_NET) {
        if (ch == '\n') net_resolve(network_domain);
        else if (ch == '\b') { if (network_domain_len) network_domain[--network_domain_len] = 0; }
        else if (ch >= 32 && ch < 127 && network_domain_len < 63) {
            network_domain[network_domain_len++] = ch; network_domain[network_domain_len] = 0;
        }
    }
    ui_dirty = 1;
}

/* ------------------------------------------------------------ inicio */
static void setw(int a, const char *t, int x, int y, int w, int h)
{
    wins[a].x = x; wins[a].y = y; wins[a].w = w; wins[a].h = h; wins[a].title = t;
    wins[a].open = wins[a].min = wins[a].zoom = 0;
}

static u32 lerp3(u32 a, u32 b, int k)   /* k 0..255 */
{
    u32 r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        r |= (u32)(ca + (cb - ca) * k / 255) << s;
    }
    return r;
}

static void make_wallpaper(void)
{
    static const u32 palettes[3][6] = {
        {0x253B79, 0xA4B2DD, 0xB8B9EA, 0x5F63B1, 0xE6BCCF, 0x8C74B7},
        {0x153C52, 0x82B8B7, 0xABD8C6, 0x367E94, 0xB9E5CE, 0x3D8B87},
        {0x593E76, 0xD5A6AB, 0xF4BB99, 0xBD7185, 0xF2D5B8, 0xBA816F}
    };
    const u32 *colors = palettes[appearance_palette];
    static int xs[2048];
    for (int x = 0; x < SW && x < 2048; x++) xs[x] = x * 1024 / SW;
    for (int y = 0; y < SH; y++) {
        int ty = y * 1024 / SH;
        for (int x = 0; x < SW; x++) {
            int xx = xs[x], yy = ty;
            int ridge = 408 + (xx - 720) * (xx - 720) / 880;
            int ridge2 = 660 + (xx - 120) * (xx - 120) / 1680;
            u32 c = lerp3(colors[0], colors[1], yy / 4);
            if (yy > ridge) {
                int k = (yy - ridge) * 3 / 4;
                if (k > 255) k = 255;
                u32 band = lerp3(colors[2], colors[3], k);
                c = lerp3(c, band, yy - ridge < 6 ? (yy - ridge) * 255 / 6 : 255);
            }
            if (yy > ridge2) {
                int k = yy - ridge2;
                if (k > 255) k = 255;
                u32 band = lerp3(colors[4], colors[5], k);
                c = lerp3(c, band, yy - ridge2 < 6 ? (yy - ridge2) * 255 / 6 : 255);
            }
            wallpaper[y * SW + x] = c;
        }
    }
}

void ui_init(void)
{
    make_wallpaper();
    mx = SW / 2; my = SH / 2;
    setw(APP_TERM,  "Terminal",     60, 70, 560, 340);
    setw(APP_NOTES, "Notas",        SW / 2 - 300, 110, 600, 360);
    setw(APP_CALC,  "Calculadora",  SW / 2 - 130, 90, 260, 380);
    setw(APP_ABOUT, "Sobre o Nimbus", SW / 2 - 220, 160, 440, 290);
    setw(APP_SETTINGS, "Ajustes", SW / 2 - 270, 65, 540, 450);
    setw(APP_NET, "Rede", SW / 2 - 280, 95, 560, 410);
    term_puts("Nimbus OS 0.1 - digite 'help'\n");
    nlen = 0;
    const char *welcome = "Bem-vindo ao Nimbus.\n\nUm lugar para suas ideias.\n\nAbra seus apps pelo dock ou use\nCtrl + Espaco para buscar.\n\nEsta nota fica apenas na memoria.";
    for (int i = 0; welcome[i]; i++) nbuf[nlen++] = welcome[i];
    rtc_read();
    open_app(APP_NOTES);
}
