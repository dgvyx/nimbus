/* Nimbus OS - motor grafico (tudo desenhado pela CPU) */
#include "nimbus.h"
#include "font.h"
#include "uifont.h"

int SW, SH;
u32 *fb;
static u32 fb_stride;                    /* em pixels */
static u32 *shown = (u32 *)0xC00000;       /* ultimo quadro enviado ao video */
static int first_frame = 1;
static int clip_x, clip_y, clip_right, clip_bottom;
static int layer_mode;
int gfx_sse2;
extern void memcpy32_sse(u32 *d, const u32 *s, u32 n);
extern void memset32_sse(u32 *d, u32 value, u32 n);
u32 *wallpaper = (u32 *)0x400000;        /* papel de parede pre-renderizado */
u32 *scene     = (u32 *)0x800000;        /* quadro montado antes de ir pra tela */

/* ---- memoria ---- */
void *memcpy(void *d, const void *s, u32 n)
{
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return r;
}
void *memset(void *d, int c, u32 n)
{
    void *r = d;
    if (n >= 64) {
        u32 bytes = n & ~3u;
        memset32(d, (u32)(u8)c * 0x01010101u, n / 4);
        d = (u8 *)d + bytes; n -= bytes;
    }
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
void *memmove(void *d, const void *s, u32 n)
{
    u8 *dd = d; const u8 *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
void memcpy32(u32 *d, const u32 *s, u32 n)
{
    if (gfx_sse2 && n >= 16) { memcpy32_sse(d, s, n); return; }
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
}

void memset32(u32 *d, u32 value, u32 n)
{
    if (gfx_sse2 && n >= 16) { memset32_sse(d, value, n); return; }
    __asm__ volatile("rep stosl" : "+D"(d), "+c"(n) : "a"(value) : "memory");
}

void gfx_init(void)
{
    volatile struct bootinfo *b = BOOTINFO;
    fb = (u32 *)b->fb;
    fb_stride = b->pitch / 4;
    SW = (int)b->width;
    SH = (int)b->height;
    gfx_clip(0, 0, SW, SH);
}

void gfx_clip(int x, int y, int w, int h)
{
    clip_x = x; clip_y = y; clip_right = x + w; clip_bottom = y + h;
}

void present(void)
{
    /* Comparar RAM e mais barato que escrever a VRAM emulada inteira. */
    for (int y = clip_y; y < clip_bottom; y++) {
        u32 *src = scene + y * SW, *old = shown + y * SW;
        int left = clip_x, right = clip_right;
        if (!first_frame) {
            while (left < right && src[left] == old[left]) left++;
            while (right > left && src[right - 1] == old[right - 1]) right--;
        }
        if (left < right) {
            memcpy32(fb + y * fb_stride + left, src + left, right - left);
            memcpy32(old + left, src + left, right - left);
        }
    }
    first_frame = 0;
}

/* ---- cores ---- */
static inline u32 mix(u32 bg, u32 fg, u32 a)   /* a: 0..256 */
{
    u32 rb = (((bg & 0xFF00FF) * (256 - a) + (fg & 0xFF00FF) * a) >> 8) & 0xFF00FF;
    u32 g  = (((bg & 0x00FF00) * (256 - a) + (fg & 0x00FF00) * a) >> 8) & 0x00FF00;
    u32 alpha = layer_mode ? ((bg >> 24) == 255 ? 255 : a + (((bg >> 24) * (256 - a)) >> 8)) << 24 : 0;
    return alpha | rb | g;
}

void gfx_layer_mode(int enabled) { layer_mode = enabled; }

void gfx_blit_layer(const u32 *pixels, int x, int y, int w, int h)
{
    int left = x < clip_x ? clip_x : x, right = x + w;
    if (right > clip_right) right = clip_right;
    if (left >= right) return;
    for (int row = 0; row < h; row++) {
        int yy = y + row;
        if (yy < clip_y || yy >= clip_bottom) continue;
        const u32 *s = pixels + row * SW + left - x;
        u32 *d = scene + yy * SW + left;
        int count = right - left;
        while (count > 0) {
            int column = (int)(s - pixels) - row * SW;
            /* O interior retangular da janela e sempre opaco. */
            if (row >= 20 && row < h - 28 && column >= 32 && column < w - 32) {
                int n = w - 32 - column;
                if (n > count) n = count;
                memcpy32(d, s, n);
                s += n; d += n; count -= n;
            } else if ((*s >> 24) == 255) {
                int n = 1;
                while (n < count && n < 16 && (s[n] >> 24) == 255) n++;
                memcpy32(d, s, n);
                s += n; d += n; count -= n;
            } else {
                u32 a = *s >> 24;
                if (a) {
                    u32 bg = *d;
                    u32 rb = (((bg & 0xFF00FF) * (256 - a)) >> 8) & 0xFF00FF;
                    u32 g = (((bg & 0x00FF00) * (256 - a)) >> 8) & 0x00FF00;
                    *d = (rb | g) + (*s & 0xFFFFFF);
                }
                s++; d++; count--;
            }
        }
    }
}

static int isqrt(int n)
{
    int r = 0;
    for (int b = 1 << 14; b; b >>= 1)
        if ((r + b) * (r + b) <= n) r += b;
    return r;
}

/* ---- formas ---- */
void fill_rect(int x, int y, int w, int h, u32 c)
{
    if (layer_mode) c |= 0xFF000000;
    int x1 = x + w, y1 = y + h;
    if (x < clip_x) x = clip_x;
    if (y < clip_y) y = clip_y;
    if (x1 > clip_right) x1 = clip_right;
    if (y1 > clip_bottom) y1 = clip_bottom;
    if (x >= x1 || y >= y1) return;
    for (int j = y; j < y1; j++) {
        u32 *p = scene + j * SW + x;
        u32 n = x1 - x;
        memset32(p, c, n);
    }
}

void fill_rect_a(int x, int y, int w, int h, u32 c, int a)
{
    if (a >= 255) { fill_rect(x, y, w, h, c); return; }
    if (a <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < clip_x) x = clip_x;
    if (y < clip_y) y = clip_y;
    if (x1 > clip_right) x1 = clip_right;
    if (y1 > clip_bottom) y1 = clip_bottom;
    if (x >= x1 || y >= y1) return;
    for (int j = y; j < y1; j++) {
        u32 *p = scene + j * SW + x;
        for (int i = x; i < x1; i++, p++) *p = mix(*p, c, a);
    }
}

/* quantos pixels recuar na linha i (0 = borda) para um canto de raio r */
static int corner_inset(int r, int i)
{
    if (r <= 0 || i >= r) return 0;
    int yc = 2 * (r - i) - 1;
    int dx = isqrt(4 * r * r - yc * yc);
    return r - (dx + 1) / 2;
}

/* retangulo com cantos arredondados: rt = raio de cima, rb = raio de baixo */
void fill_round(int x, int y, int w, int h, int rt, int rb, u32 c, int a)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < clip_y || yy >= clip_bottom) continue;
        int in = 0;
        if (j < rt) in = corner_inset(rt, j);
        int jb = h - 1 - j;
        if (jb < rb) { int t = corner_inset(rb, jb); if (t > in) in = t; }
        fill_rect_a(x + in, yy, w - 2 * in, 1, c, a);
    }
}

void fill_circle(int cx, int cy, int r, u32 c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = isqrt(r * r + r - dy * dy);
        fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

/* ---- texto ---- */
int ui_text_width(const char *text)
{
    int width = 0;
    while (*text) {
        unsigned char c = *text++;
        if (c < 32 || c > 126) c = '?';
        width += ui_glyph_width[c - 32];
    }
    return width;
}

void ui_text(int x, int y, const char *text, u32 color)
{
    while (*text) {
        unsigned char c = *text++;
        if (c < 32 || c > 126) c = '?';
        int width = ui_glyph_width[c - 32];
        const u8 *alpha = ui_glyph_alpha + ui_glyph_offset[c - 32];
        for (int j = 0; j < 22; j++) {
            int yy = y + j;
            if (yy < clip_y || yy >= clip_bottom) continue;
            for (int i = 0; i < width; i++) {
                int xx = x + i, a = alpha[j * width + i];
                if (!a || xx < clip_x || xx >= clip_right) continue;
                u32 *p = scene + yy * SW + xx;
                *p = a == 255 ? color | (layer_mode ? 0xFF000000 : 0) : mix(*p, color, a);
            }
        }
        x += width;
    }
}

void draw_char(int x, int y, char ch, u32 c, int s)
{
    if (ch < 32 || ch > 126) ch = '?';
    const unsigned char *g = font5x7[ch - 32];
    int desc = (ch == 'g' || ch == 'j' || ch == 'p' || ch == 'q' || ch == 'y');
    int oy = desc ? 2 * s : 0;
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (g[r] & (0x10 >> k))
                fill_rect(x + k * s, y + oy + r * s, s, s, c);
    if (ch == 'j') fill_rect(x + 3 * s, y, s, s, c);       /* pingo do j */
}

void draw_text_n(int x, int y, const char *t, u32 c, int s, int maxn)
{
    for (int i = 0; t[i] && i < maxn; i++)
        draw_char(x + i * 6 * s, y, t[i], c, s);
}

void draw_text(int x, int y, const char *t, u32 c, int s)
{
    draw_text_n(x, y, t, c, s, 1 << 20);
}

/* ---- cursor do mouse (desenhado direto no framebuffer) ---- */
#define CUR_W 12
#define CUR_H 18
static const char *cur_img[CUR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX  X..X    ",
    "X    X..X   ",
    "     X..X   ",
    "      X..X  ",
    "      XXXX  ",
};

void cursor_restore(int x, int y)
{
    for (int j = 0; j < CUR_H; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) continue;
        for (int i = 0; i < CUR_W; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SW) continue;
            fb[yy * fb_stride + xx] = scene[yy * SW + xx];
        }
    }
}

void cursor_draw(int x, int y)
{
    for (int j = 0; j < CUR_H; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) continue;
        for (int i = 0; i < CUR_W; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SW) continue;
            char c = cur_img[j][i];
            if (c == 'X') fb[yy * fb_stride + xx] = 0x000000;
            else if (c == '.') fb[yy * fb_stride + xx] = 0xFFFFFF;
        }
    }
}
