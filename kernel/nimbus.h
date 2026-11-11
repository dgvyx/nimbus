#ifndef NIMBUS_H
#define NIMBUS_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* preenchido pelo bootloader em 0x600 */
struct bootinfo { u32 magic, fb, pitch, width, height, bpp; };
#define BOOTINFO ((volatile struct bootinfo *)0x600)
#define BOOT_MAGIC 0x4E494D42

static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
#define TICK_HZ 240
extern volatile u32 timer_ticks;
extern u32 rendered_frames, render_ticks;

void *memcpy(void *d, const void *s, u32 n);
void *memset(void *d, int c, u32 n);
void *memmove(void *d, const void *s, u32 n);
void memcpy32(u32 *d, const u32 *s, u32 n);
void memset32(u32 *d, u32 value, u32 n);

/* memoria */
void mem_init(void);
void *kmalloc(u32 size);
void kfree(void *ptr);
void *krealloc(void *ptr, u32 new_size);
void mem_stats(void);
u32 strlen(const char *s);

/* graficos */
#define RGB(r, g, b) ((u32)(((r) << 16) | ((g) << 8) | (b)))
extern int SW, SH;
extern u32 *fb, *scene, *wallpaper;
void gfx_init(void);
void present(void);
void gfx_clip(int x, int y, int w, int h);
void gfx_layer_mode(int enabled);
void gfx_blit_layer(const u32 *pixels, int x, int y, int w, int h);
void ui_text(int x, int y, const char *text, u32 color);
int ui_text_width(const char *text);
void fill_rect(int x, int y, int w, int h, u32 c);
void fill_rect_a(int x, int y, int w, int h, u32 c, int a);
void fill_round(int x, int y, int w, int h, int rt, int rb, u32 c, int a);
void fill_circle(int cx, int cy, int r, u32 c);
void draw_char(int x, int y, char ch, u32 c, int s);
void draw_text(int x, int y, const char *t, u32 c, int s);
void draw_text_n(int x, int y, const char *t, u32 c, int s, int maxn);
void cursor_restore(int x, int y);
void cursor_draw(int x, int y);
void term_puts(const char *s);
void itoa_(int v, char *o);

/* entrada */
void input_init(void);
void input_poll(void);

/* interface */
extern int ui_dirty;
void ui_init(void);
void ui_render(void);
void ui_tick(void);
void ui_animate(void);
void ui_shortcut(int key);
void ui_on_mouse(int dx, int dy, int btn);
void ui_on_key(char ch);
int  ui_mouse_x(void);
int  ui_mouse_y(void);

#endif
