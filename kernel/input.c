/* Nimbus OS - teclado e mouse PS/2 (por polling, sem interrupcoes por enquanto) */
#include "nimbus.h"

static int wait_w(void) { for (int i = 0; i < 200000; i++) if (!(inb(0x64) & 2)) return 1; return 0; }
static int wait_r(void) { for (int i = 0; i < 200000; i++) if (inb(0x64) & 1) return 1; return 0; }
static void kbc_cmd(u8 c)  { wait_w(); outb(0x64, c); }
static void kbc_data(u8 d) { wait_w(); outb(0x60, d); }
static int mouse_cmd(u8 c)
{
    kbc_cmd(0xD4);
    kbc_data(c);
    return wait_r() ? inb(0x60) : -1;
}

void input_init(void)
{
    while (inb(0x64) & 1) inb(0x60);     /* esvazia o buffer */
    kbc_cmd(0xA8);                       /* habilita a porta do mouse */
    kbc_cmd(0x20);
    wait_r();
    u8 cfg = inb(0x60);
    cfg &= ~0x33;                        /* sem IRQ, relogios ligados */
    cfg |= 0x40;                         /* traducao: scancodes set 1 */
    kbc_cmd(0x60);
    kbc_data(cfg);
    mouse_cmd(0xF6);                     /* padroes */
    mouse_cmd(0xE6);                     /* movimento 1:1, sem escala 2:1 */
    mouse_cmd(0xF3); mouse_cmd(200);      /* 200 amostras/s */
    mouse_cmd(0xF4);                     /* liga o envio de pacotes */
}

/* layout US (ABNT2 fica pra depois) */
static const char km0[] = "\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char km1[] = "\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static void on_kbd(u8 sc)
{
    static int shift, e0, ctrl, alt;
    if (sc == 0xE0) { e0 = 1; return; }
    int rel = sc & 0x80, code = sc & 0x7F;
    if (code == 0x1D) { ctrl = !rel; e0 = 0; return; }
    if (code == 0x38) { alt = !rel; e0 = 0; return; }
    if (e0) {
        e0 = 0;
        if (!rel && code == 0x48) ui_shortcut(5);
        if (!rel && code == 0x50) ui_shortcut(6);
        return;
    }
    if (code == 0x2A || code == 0x36) {
        int mask = code == 0x2A ? 1 : 2;
        if (rel) shift &= ~mask; else shift |= mask;
        return;
    }
    if (rel) return;
    if (ctrl && code == 0x39) { ui_shortcut(1); return; }
    if (alt && code == 0x0F) { ui_shortcut(2); return; }
    if (alt && code == 0x3E) { ui_shortcut(3); return; }
    if (ctrl && code == 0x32) { ui_shortcut(4); return; }
    if (code < 58) {
        char ch = shift ? km1[code] : km0[code];
        if (ch) ui_on_key(ch);
    }
}

static void on_mouse(u8 b)
{
    static u8 pkt[3];
    static int n;
    if (n == 0 && !(b & 8)) return;      /* fora de sincronia */
    pkt[n++] = b;
    if (n < 3) return;
    n = 0;
    if (pkt[0] & 0xC0) return;           /* overflow: descarta */
    int dx = pkt[1], dy = pkt[2];
    if (pkt[0] & 0x10) dx -= 256;
    if (pkt[0] & 0x20) dy -= 256;
    ui_on_mouse(dx, -dy, pkt[0] & 7);
}

void input_poll(void)
{
    for (int guard = 0; guard < 64; guard++) {
        u8 st = inb(0x64);
        if (!(st & 1)) break;
        u8 d = inb(0x60);
        if (st & 0x20) on_mouse(d);
        else on_kbd(d);
    }
}
