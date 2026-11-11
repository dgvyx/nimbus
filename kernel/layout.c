#include "layout.h"

static u32 text_length(const char *s) {
    u32 n = 0;
    if (s) while (s[n]) n++;
    return n;
}

static int utf8_decode(const char *s, u32 remaining, u32 *cp) {
    if (!remaining) { *cp = 0; return 0; }
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0 && remaining >= 2 && ((unsigned char)s[1] & 0xC0) == 0x80) {
        *cp = ((u32)(c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0 && remaining >= 3 && ((unsigned char)s[1] & 0xC0) == 0x80 &&
        ((unsigned char)s[2] & 0xC0) == 0x80) {
        *cp = ((u32)(c & 0x0F) << 12) | (((u32)(unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && remaining >= 4 && ((unsigned char)s[1] & 0xC0) == 0x80 &&
        ((unsigned char)s[2] & 0xC0) == 0x80 && ((unsigned char)s[3] & 0xC0) == 0x80) {
        *cp = ((u32)(c & 7) << 18) | (((u32)(unsigned char)s[1] & 0x3F) << 12) |
              (((u32)(unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
        return 4;
    }
    *cp = c;
    return 1;
}

static int char_width(int font_size, int monospace) {
    return monospace ? font_size : font_size;
}

static int measure_text_width(const char *text, u32 len, int font_size, int monospace) {
    int width = 0;
    for (u32 i = 0; i < len;) {
        u32 cp;
        int count = utf8_decode(text + i, len - i, &cp);
        if (!count) break;
        i += (u32)count;
        if (cp == '\t') width += (font_size / 2) * 4;
        else if (cp >= 32) width += char_width(font_size, monospace);
    }
    return width;
}

static void layout_add_item(layout_engine_t *engine, layout_item_t item) {
    if (engine->item_count < LAYOUT_MAX_ITEMS) engine->items[engine->item_count++] = item;
}

static int line_start_x(const layout_engine_t *engine) {
    if (engine->list_depth > 0 && engine->list_depth <= 8) return engine->list_start_x[engine->list_depth - 1];
    return 0;
}

static void layout_break_line(layout_engine_t *engine) {
    engine->cursor_x = line_start_x(engine);
    engine->cursor_y += engine->line_height;
    engine->line_height = engine->base_font_size + 4;
    engine->pending_space = 0;
}

static int layout_get_font_size(layout_engine_t *engine, int heading_level) {
    static const int sizes[] = {0, 32, 28, 24, 20, 18, 16};
    if (heading_level >= 1 && heading_level <= 6) return sizes[heading_level];
    return engine->base_font_size;
}

static void layout_add_space(layout_engine_t *engine, int width, int font_size) {
    if (width <= 0) return;
    if (engine->width > 0 && engine->cursor_x > line_start_x(engine) && engine->cursor_x + width > engine->width)
        layout_break_line(engine);
    if (width > engine->width - engine->cursor_x) width = engine->width - engine->cursor_x;
    if (width <= 0) return;
    layout_item_t item = {0};
    item.type = LAYOUT_ITEM_SPACE;
    item.x = engine->cursor_x;
    item.y = engine->cursor_y;
    item.w = width;
    item.h = font_size + 2;
    item.color = engine->text_color;
    layout_add_item(engine, item);
    if (item.h > engine->line_height) engine->line_height = item.h;
    engine->cursor_x += width;
}

static void layout_add_run(layout_engine_t *engine, const char *text, u32 len,
                           int bold, int italic, int monospace, int heading_level) {
    int font_size = layout_get_font_size(engine, heading_level);
    int max_width = engine->width - line_start_x(engine);
    u32 offset = 0;
    while (offset < len) {
        u32 start = offset;
        int run_width = 0;
        while (offset < len) {
            u32 cp;
            int count = utf8_decode(text + offset, len - offset, &cp);
            if (!count) break;
            int cw = cp == '\t' ? (font_size / 2) * 4 : (cp >= 32 ? char_width(font_size, monospace) : 0);
            if (cw > max_width) cw = max_width;
            if (run_width + cw > max_width && offset > start) break;
            if (engine->cursor_x > line_start_x(engine) && engine->cursor_x + run_width + cw > engine->width)
                break;
            run_width += cw;
            offset += (u32)count;
        }
        if (offset == start) {
            if (engine->cursor_x > line_start_x(engine)) {
                layout_break_line(engine);
                continue;
            }
            u32 ignored;
            int count = utf8_decode(text + offset, len - offset, &ignored);
            if (!count) break;
            offset += (u32)count;
            run_width = char_width(font_size, monospace);
        }
        layout_item_t item = {0};
        item.type = engine->in_link && engine->current_link_url ? LAYOUT_ITEM_LINK : LAYOUT_ITEM_TEXT;
        item.x = engine->cursor_x;
        item.y = engine->cursor_y;
        item.w = run_width;
        item.h = font_size + 2;
        item.color = item.type == LAYOUT_ITEM_LINK ? engine->link_color : engine->text_color;
        if (item.type == LAYOUT_ITEM_LINK) {
            item.data.link.url = engine->current_link_url;
            item.data.link.url_len = engine->current_link_url_len;
            item.data.link.text = text + start;
            item.data.link.text_len = offset - start;
            item.data.link.bold = bold;
            item.data.link.italic = italic;
            item.data.link.monospace = monospace;
            item.data.link.heading_level = heading_level;
        } else {
            item.data.text.text = text + start;
            item.data.text.len = offset - start;
            item.data.text.bold = bold;
            item.data.text.italic = italic;
            item.data.text.monospace = monospace;
            item.data.text.heading_level = heading_level;
        }
        layout_add_item(engine, item);
        if (item.h > engine->line_height) engine->line_height = item.h;
        engine->cursor_x += run_width;
        if (offset < len && engine->cursor_x >= engine->width) layout_break_line(engine);
    }
}

static void layout_add_text(layout_engine_t *engine, const char *text, u32 len,
                            int bold, int italic, int monospace, int heading_level) {
    int font_size = layout_get_font_size(engine, heading_level);
    u32 pos = 0;
    while (pos < len) {
        u32 cp;
        int count = utf8_decode(text + pos, len - pos, &cp);
        if (!count) break;
        if ((cp == '\n' || cp == '\r') && engine->in_pre) {
            pos += (u32)count;
            if (cp == '\r' && pos < len && text[pos] == '\n') pos++;
            layout_break_line(engine);
            continue;
        }
        if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\f') {
            if (engine->in_pre) {
                int width = cp == '\t' ? (font_size / 2) * 4 : font_size / 2;
                layout_add_space(engine, width, font_size);
            } else {
                engine->pending_space = 1;
            }
            pos += (u32)count;
            continue;
        }
        u32 start = pos;
        while (pos < len) {
            count = utf8_decode(text + pos, len - pos, &cp);
            if (!count || cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\f') break;
            pos += (u32)count;
        }
        int word_width = measure_text_width(text + start, pos - start, font_size, monospace);
        int gap = engine->pending_space && engine->cursor_x > line_start_x(engine) ? font_size / 2 : 0;
        if (!engine->in_pre && engine->cursor_x > line_start_x(engine) &&
            word_width + gap > engine->width - engine->cursor_x) {
            layout_break_line(engine);
            gap = 0;
        }
        if (gap) layout_add_space(engine, gap, font_size);
        engine->pending_space = 0;
        layout_add_run(engine, text + start, pos - start, bold, italic, monospace, heading_level);
    }
}

static char *layout_decode_text(layout_engine_t *engine, const char *src, u32 len, u32 *out_len) {
    if (engine->text_storage_used >= LAYOUT_TEXT_STORAGE_SIZE) { *out_len = 0; return 0; }
    u32 available = LAYOUT_TEXT_STORAGE_SIZE - engine->text_storage_used;
    if (len < available) available = len + 1;
    char *dst = engine->text_storage + engine->text_storage_used;
    html_decode_text(dst, available, src, len);
    *out_len = text_length(dst);
    engine->text_storage_used += *out_len + 1;
    return dst;
}

static void layout_process_children(layout_engine_t *engine, html_node_t *node) {
    for (html_node_t *child = node->children; child; child = child->next_sibling) layout_process_node(engine, child);
}

static void layout_process_a(layout_engine_t *engine, html_node_t *node) {
    const char *href = 0;
    u32 href_len = 0;
    for (u32 i = 0; i < node->attr_count; i++) {
        if (node->attrs[i].type == HTML_ATTR_HREF) { href = node->attrs[i].value; href_len = node->attrs[i].value_len; break; }
    }
    int old_in_link = engine->in_link;
    const char *old_url = engine->current_link_url;
    u32 old_url_len = engine->current_link_url_len;
    if (href) href = layout_decode_text(engine, href, href_len, &href_len);
    engine->in_link = href != 0;
    engine->current_link_url = href;
    engine->current_link_url_len = href_len;
    layout_process_children(engine, node);
    engine->in_link = old_in_link;
    engine->current_link_url = old_url;
    engine->current_link_url_len = old_url_len;
}

static void layout_process_text_node(layout_engine_t *engine, html_node_t *node) {
    if (!node->text_content || !node->text_len) return;
    u32 len = 0;
    char *decoded = layout_decode_text(engine, node->text_content, node->text_len, &len);
    if (!decoded || !len) return;
    int bold = 0, italic = 0, monospace = 0, heading = 0;
    for (html_node_t *p = node->parent; p; p = p->parent) {
        if (p->tag == HTML_TAG_B || p->tag == HTML_TAG_STRONG) bold = 1;
        if (p->tag == HTML_TAG_I || p->tag == HTML_TAG_EM) italic = 1;
        if (p->tag == HTML_TAG_PRE) monospace = 1;
        if (p->tag >= HTML_TAG_H1 && p->tag <= HTML_TAG_H6) {
            if (!heading) heading = p->tag - HTML_TAG_H1 + 1;
            bold = 1;
        }
    }
    layout_add_text(engine, decoded, len, bold, italic, monospace, heading);
}

static void layout_process_hr(layout_engine_t *engine) {
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
    layout_item_t item = {0};
    item.type = LAYOUT_ITEM_HR;
    item.x = 0;
    item.y = engine->cursor_y + engine->line_height / 2;
    item.w = engine->width;
    item.h = 2;
    item.color = engine->hr_color;
    layout_add_item(engine, item);
    engine->cursor_y += engine->line_height;
    engine->cursor_x = line_start_x(engine);
}

static void layout_process_li(layout_engine_t *engine, html_node_t *node) {
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
    int depth = engine->list_depth;
    if (depth < 8) {
        engine->list_start_x[depth] = engine->cursor_x + engine->list_bullet_width;
        if (engine->list_start_x[depth] >= engine->width) engine->list_start_x[depth] = engine->width - 1;
        engine->list_depth++;
    }
    const char *marker = "\xE2\x80\xA2";
    u32 marker_len = 3;
    if (depth < 8 && engine->list_ordered[depth]) {
        if (engine->text_storage_used + 12 < LAYOUT_TEXT_STORAGE_SIZE) {
            char *number = engine->text_storage + engine->text_storage_used;
            u32 value = ++engine->list_counter[depth];
            char digits[10];
            u32 count = 0;
            do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
            for (u32 i = 0; i < count; i++) number[i] = digits[count - i - 1];
            number[count] = '.';
            number[count + 1] = ' ';
            number[count + 2] = 0;
            marker = number;
            marker_len = count + 2;
            engine->text_storage_used += marker_len + 1;
        }
    }
    layout_item_t bullet = {0};
    bullet.type = LAYOUT_ITEM_TEXT;
    bullet.x = engine->cursor_x;
    bullet.y = engine->cursor_y;
    bullet.w = engine->list_bullet_width;
    bullet.h = engine->base_font_size + 2;
    bullet.color = engine->text_color;
    bullet.data.text.text = marker;
    bullet.data.text.len = marker_len;
    bullet.w = measure_text_width(marker, marker_len, engine->base_font_size, 0);
    if (bullet.w > engine->width - bullet.x) bullet.w = engine->width - bullet.x;
    layout_add_item(engine, bullet);
    if (depth < 8) {
        engine->list_start_x[depth] = bullet.x + bullet.w;
        if (engine->list_start_x[depth] >= engine->width) engine->list_start_x[depth] = engine->width - 1;
    }
    engine->cursor_x = line_start_x(engine);
    layout_process_children(engine, node);
    if (engine->cursor_x > line_start_x(engine) || engine->cursor_y == bullet.y) layout_break_line(engine);
    if (engine->list_depth > (depth < 8 ? depth : 8)) engine->list_depth = depth;
    engine->cursor_x = line_start_x(engine);
}

static void layout_process_list(layout_engine_t *engine, html_node_t *node) {
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
    int depth = engine->list_depth < 8 ? engine->list_depth : 7;
    engine->list_ordered[depth] = node->tag == HTML_TAG_OL;
    engine->list_counter[depth] = 0;
    for (html_node_t *child = node->children; child; child = child->next_sibling)
        if (child->tag == HTML_TAG_LI) layout_process_li(engine, child);
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
}

static void layout_process_block(layout_engine_t *engine, html_node_t *node) {
    engine->pending_space = 0;
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
    layout_process_children(engine, node);
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
    engine->pending_space = 0;
}

static void layout_process_pre(layout_engine_t *engine, html_node_t *node) {
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
    int old = engine->in_pre;
    engine->in_pre = 1;
    layout_process_children(engine, node);
    engine->in_pre = old;
    if (engine->cursor_x > line_start_x(engine)) layout_break_line(engine);
}

void layout_process_node(layout_engine_t *engine, html_node_t *node) {
    if (!engine || !node) return;
    if (node->is_text_node) { layout_process_text_node(engine, node); return; }
    switch (node->tag) {
        case HTML_TAG_BR: layout_break_line(engine); break;
        case HTML_TAG_HR: layout_process_hr(engine); break;
        case HTML_TAG_A: layout_process_a(engine, node); break;
        case HTML_TAG_PRE: layout_process_pre(engine, node); break;
        case HTML_TAG_LI: layout_process_li(engine, node); break;
        case HTML_TAG_UL:
        case HTML_TAG_OL: layout_process_list(engine, node); break;
        case HTML_TAG_P:
        case HTML_TAG_DIV:
        case HTML_TAG_H1:
        case HTML_TAG_H2:
        case HTML_TAG_H3:
        case HTML_TAG_H4:
        case HTML_TAG_H5:
        case HTML_TAG_H6: layout_process_block(engine, node); break;
        case HTML_TAG_HEAD:
        case HTML_TAG_SCRIPT:
        case HTML_TAG_STYLE: break;
        default: layout_process_children(engine, node); break;
    }
}

void layout_init(layout_engine_t *engine, int width) {
    engine->item_count = 0;
    engine->width = width > 0 ? width : 1;
    engine->cursor_x = 0;
    engine->cursor_y = 0;
    engine->line_height = 20;
    engine->pending_space = 0;
    engine->base_font_size = 16;
    engine->text_color = RGB(0, 0, 0);
    engine->link_color = RGB(0, 0, 255);
    engine->hr_color = RGB(128, 128, 128);
    engine->in_pre = 0;
    engine->in_link = 0;
    engine->current_link_url = 0;
    engine->current_link_url_len = 0;
    engine->list_depth = 0;
    engine->list_bullet_width = 16;
    engine->text_storage_used = 0;
    for (int i = 0; i < 8; i++) {
        engine->list_start_x[i] = 0;
        engine->list_ordered[i] = 0;
        engine->list_counter[i] = 0;
    }
}

void layout_finalize(layout_engine_t *engine) { engine->pending_space = 0; }

int layout_get_height(const layout_engine_t *engine) {
    int max_y = engine->cursor_y;
    for (u32 i = 0; i < engine->item_count; i++) {
        int bottom = engine->items[i].y + engine->items[i].h;
        if (bottom > max_y) max_y = bottom;
    }
    return max_y;
}

void layout_render(const layout_engine_t *engine, int scroll_y) {
    (void)engine;
    (void)scroll_y;
}
