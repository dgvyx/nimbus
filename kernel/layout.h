#ifndef LAYOUT_H
#define LAYOUT_H

#include "nimbus.h"
#include "html.h"

#define LAYOUT_MAX_ITEMS 1024
#define LAYOUT_TEXT_STORAGE_SIZE 16384

typedef enum {
    LAYOUT_ITEM_TEXT = 0,
    LAYOUT_ITEM_LINK,
    LAYOUT_ITEM_HR,
    LAYOUT_ITEM_SPACE,
} layout_item_type_t;

typedef struct {
    layout_item_type_t type;
    int x, y;
    int w, h;
    u32 color;
    union {
        struct {
            const char *text;
            u32 len;
            int bold;
            int italic;
            int monospace;
            int heading_level;
        } text;
        struct {
            const char *url;
            u32 url_len;
            const char *text;
            u32 text_len;
            int bold, italic, monospace, heading_level;
        } link;
    } data;
} layout_item_t;

typedef struct {
    layout_item_t items[LAYOUT_MAX_ITEMS];
    char text_storage[LAYOUT_TEXT_STORAGE_SIZE];
    u32 text_storage_used;
    u32 item_count;
    int width;
    int cursor_x;
    int cursor_y;
    int line_height;
    int pending_space;
    int base_font_size;
    u32 text_color;
    u32 link_color;
    u32 hr_color;
    int in_pre;
    int in_link;
    const char *current_link_url;
    u32 current_link_url_len;
    int list_depth;
    int list_start_x[8];
    int list_ordered[8];
    u32 list_counter[8];
    int list_bullet_width;
} layout_engine_t;

/* Fixed-capacity, allocation-free output. Text and URLs live in text_storage
 * until layout_init; do not copy/move an initialized engine. A glyph wider
 * than the viewport has its item box clipped to the available width. */

void layout_init(layout_engine_t *engine, int width);
void layout_process_node(layout_engine_t *engine, html_node_t *node);
void layout_finalize(layout_engine_t *engine);

int layout_get_height(const layout_engine_t *engine);
void layout_render(const layout_engine_t *engine, int scroll_y);

#endif
