#include <stdio.h>
#include "../kernel/layout.h"
static html_parser_t p;
static layout_engine_t e;
#define CHECK(x) do { if (!(x)) { printf("line %d failed\n", __LINE__); return 1; } } while (0)
static void layout(const char *s, int w) {
    u32 n = 0; while (s[n]) n++;
    html_parser_init(&p, s, n);
    layout_init(&e, w);
    layout_process_node(&e, html_parse(&p));
    layout_finalize(&e);
}
int main(void) {
    layout("a <b>b</b> <i>c</i>", 100);
    CHECK(e.item_count == 5);
    CHECK(e.items[1].type == LAYOUT_ITEM_SPACE && e.items[2].x == 24);
    CHECK(e.items[2].data.text.bold && e.items[4].data.text.italic);
    layout("aa bb", 56);
    CHECK(e.item_count == 2 && e.items[1].x == 0 && e.items[1].y == 20);
    layout("<h1>A<br>B</h1><p>C</p>", 80);
    CHECK(e.items[1].y >= e.items[0].y + e.items[0].h);
    CHECK(e.items[2].y >= e.items[1].y + e.items[1].h);
    layout("<a href='x&amp;y'><b>abcdef</b></a>", 32);
    CHECK(e.item_count == 3);
    for (u32 i = 0; i < e.item_count; i++) {
        CHECK(e.items[i].type == LAYOUT_ITEM_LINK);
        CHECK(e.items[i].data.link.bold && e.items[i].data.link.url_len == 3);
        CHECK(e.items[i].data.link.url[1] == '&');
        CHECK(e.items[i].data.link.text[0] == "ace"[i]);
    }
    layout("<pre>  a\n\n\tb</pre>", 100);
    CHECK(e.items[2].x == 16 && e.items[4].y == 40 && e.items[4].x == 32);
    layout("<ol><li>a<ul><li>b</ul><li>c</ol>", 200);
    CHECK(e.items[0].data.text.text[0] == '1');
    CHECK(e.items[4].data.text.text[0] == '2');
    layout("<head>hidden</head><p>&lt;&amp;</p><hr>", 20);
    CHECK(e.items[0].data.text.text[0] == '<');
    CHECK(e.items[e.item_count - 1].type == LAYOUT_ITEM_HR);
    layout("<ul><li>abcdefgh</ul>", 1);
    for (u32 i = 0; i < e.item_count; i++) CHECK(e.items[i].x + e.items[i].w <= 1);
    puts("layout edges passed");
    return 0;
}
