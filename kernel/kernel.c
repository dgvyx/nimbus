/* Nimbus OS - kernel principal */
#include "nimbus.h"
#include "net.h"

struct idt_entry { u16 lo, sel; u8 zero, flags; u16 hi; } __attribute__((packed));
struct idtr_t { u16 limit; u32 base; } __attribute__((packed));
static struct idt_entry idt[33];
extern void isr_stub(void);
extern void timer_stub(void);
volatile u32 timer_ticks;
u32 rendered_frames, render_ticks;

/* excecao da CPU: pinta a tela de vermelho e para (facilita achar bugs no hardware real) */
void fault_handler(void)
{
    volatile struct bootinfo *b = BOOTINFO;
    u32 *p = (u32 *)b->fb;
    u32 n = (b->pitch / 4) * b->height;
    for (u32 i = 0; i < n; i++) p[i] = 0x00B00020;
    for (;;) __asm__ volatile("hlt");
}

static void idt_init(void)
{
    for (int i = 0; i < 33; i++) {
        u32 a = (u32)(i == 32 ? timer_stub : isr_stub);
        idt[i].lo = a & 0xFFFF;
        idt[i].sel = 0x08;
        idt[i].zero = 0;
        idt[i].flags = 0x8E;
        idt[i].hi = a >> 16;
    }
    struct idtr_t r = { sizeof(idt) - 1, (u32)idt };
    __asm__ volatile("lidt %0" :: "m"(r));
}

static void pic_write(u16 port, u8 value)
{
    outb(port, value);
    outb(0x80, 0);                 /* espera de I/O para o PIC */
}

static void timer_init(void)
{
    /* Remapeia o PIC; apenas IRQ0 (PIT) fica habilitada. */
    pic_write(0x20, 0x11); pic_write(0xA0, 0x11);
    pic_write(0x21, 0x20); pic_write(0xA1, 0x28);
    pic_write(0x21, 4); pic_write(0xA1, 2);
    pic_write(0x21, 1); pic_write(0xA1, 1);
    pic_write(0x21, 0xFE); pic_write(0xA1, 0xFF);
    u16 divisor = 1193182 / TICK_HZ;
    outb(0x43, 0x36);
    outb(0x40, divisor & 255);
    outb(0x40, divisor >> 8);
    __asm__ volatile("sti" ::: "memory");
}

void kmain(void)
{
    if (BOOTINFO->magic != BOOT_MAGIC) for (;;) __asm__ volatile("hlt");
    idt_init();
    gfx_init();
    ui_init();
    input_init();
    net_init();

    ui_render();
    present();
    int cx = ui_mouse_x(), cy = ui_mouse_y();
    cursor_draw(cx, cy);
    ui_dirty = 0;

    mem_init();
    timer_init();
    u32 last_clock = timer_ticks, last_frame = timer_ticks;
    for (;;) {
        input_poll();
        net_poll();
        u32 now = timer_ticks;
        if (now - last_clock >= TICK_HZ) { ui_tick(); last_clock = now; }
        if (now - last_frame >= TICK_HZ / 60) ui_animate();

        if (ui_dirty && now - last_frame >= TICK_HZ / 60) {
            cursor_restore(cx, cy);
            ui_render();
            present();
            render_ticks += timer_ticks - now;
            rendered_frames++;
            cx = ui_mouse_x(); cy = ui_mouse_y();
            cursor_draw(cx, cy);
            ui_dirty = 0;
        } else if (cx != ui_mouse_x() || cy != ui_mouse_y()) {
            cursor_restore(cx, cy);
            cx = ui_mouse_x(); cy = ui_mouse_y();
            cursor_draw(cx, cy);
        }
        if (now - last_frame >= TICK_HZ / 60) last_frame = now;
        __asm__ volatile("hlt" ::: "memory");
    }
}
