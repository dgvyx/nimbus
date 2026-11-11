#include <stdio.h>
#include "../kernel/html.h"
static html_parser_t p;
#define CHECK(x) do { if (!(x)) { printf("line %d failed\n", __LINE__); return 1; } } while (0)
static html_node_t *parse(const char *s) {
    u32 n = 0; while (s[n]) n++;
    html_parser_init(&p, s, n); return html_parse(&p);
}
int main(void) {
    char out[16];
    const char slice[] = {'&','a','m','p',';'};
    html_decode_text(out, sizeof(out), slice, sizeof(slice));
    CHECK(out[0] == '&' && !out[1]);
    html_decode_text(out, sizeof(out), slice, 3);
    CHECK(out[0] == '&' && out[1] == 'a' && out[2] == 'm' && !out[3]);
    html_node_t *r = parse("<SCRIPT x='>'>hidden</ScRiPt><STYLE>bad</STYLE><p>ok");
    CHECK(r->children->tag == HTML_TAG_P);
    r = parse("<ul><li>a<ul><li>b</ul><li>c</ul>");
    html_node_t *li = r->children->children;
    CHECK(li->children->next_sibling->tag == HTML_TAG_UL);
    CHECK(li->next_sibling->tag == HTML_TAG_LI);
    r = parse("a < b<");
    CHECK(p.node_count == 5 && r->children->text_len == 2);
    r = parse("<p>a<div>b</div>");
    CHECK(r->children->next_sibling->tag == HTML_TAG_DIV);
    puts("parser edges passed");
    return 0;
}
