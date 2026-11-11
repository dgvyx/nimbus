#include <stdio.h>
#include <stdlib.h>
#include "../kernel/layout.h"
static html_parser_t p;
static layout_engine_t e;
static char large[40000];
#define CHECK(x) do { if (!(x)) { printf("line %d failed\n", __LINE__); return 1; } } while (0)
static int exercise(const char *s, u32 n, int width) {
    html_parser_init(&p, s, n);
    layout_init(&e, width);
    layout_process_node(&e, html_parse(&p));
    layout_finalize(&e);
    CHECK(p.node_count <= HTML_MAX_ELEMENTS && p.pos <= n);
    CHECK(e.item_count <= LAYOUT_MAX_ITEMS && e.text_storage_used <= LAYOUT_TEXT_STORAGE_SIZE);
    for (u32 i = 0; i < e.item_count; i++) {
        layout_item_t *it = &e.items[i];
        CHECK(it->x >= 0 && it->y >= 0 && it->w >= 0 && it->h > 0);
        CHECK(it->x + it->w <= e.width);
    }
    return 0;
}
int main(void) {
    const char sample[] = "<p>a &amp;<b>b</p><SCRIPT x='>'>bad</SCRIPT><ol><li>a<li><pre> b\n\tc</pre></ol><a href='x&amp;y'>abcdef</a><!--x-->";
    for (u32 n = 0; n < sizeof(sample); n++) {
        char *s = malloc(n ? n : 1);
        CHECK(s != 0);
        for (u32 i = 0; i < n; i++) s[i] = sample[i];
        int failed = exercise(s, n, (int)(n % 80));
        free(s);
        CHECK(!failed);
    }
    for (u32 i = 0; i < sizeof(large); i++) large[i] = 'x';
    CHECK(!exercise(large, sizeof(large), 1));
    CHECK(e.item_count == LAYOUT_MAX_ITEMS);
    for (u32 i = 0; i < sizeof(large); i++) large[i] = "<b>x"[i % 4];
    CHECK(!exercise(large, sizeof(large), 50));
    CHECK(p.node_count == HTML_MAX_ELEMENTS);
    u32 seed = 1234;
    const char alphabet[] = "<>/!-'\"=;& abcdef012\n\t";
    for (u32 trial = 0; trial < 1000; trial++) {
        u32 n = trial % 257;
        for (u32 i = 0; i < n; i++) {
            seed = seed * 1664525U + 1013904223U;
            large[i] = alphabet[seed % (sizeof(alphabet) - 1)];
        }
        CHECK(!exercise(large, n, (int)(trial % 100)));
    }
    puts("capacity and malformed-input tests passed");
    return 0;
}
