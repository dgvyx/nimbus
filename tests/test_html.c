#include <stdio.h>
#include "../kernel/nimbus.h"

static u32 test_strlen(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static int test_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int test_strncmp(const char *a, const char *b, u32 n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (u32)-1) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

#define strlen test_strlen
#define strcmp test_strcmp
#define strncmp test_strncmp
#include "../kernel/html.h"
#include "../kernel/layout.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            return 1; \
        } else { \
            printf("PASS: %s\n", msg); \
        } \
    } while (0)

int test_basic_parsing() {
    const char *html = "<html><body><p>Hello World</p></body></html>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Root node created");
    TEST_ASSERT(root->tag == HTML_TAG_BODY || root->tag == HTML_TAG_HTML, "Root is body or html");
    html_free(&parser);
    return 0;
}

int test_headings() {
    const char *html = "<h1>Title</h1><h2>Subtitle</h2><h3>Sub</h3><h4>Sub</h4><h5>Sub</h5><h6>Sub</h6>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Headings parsed");
    int count = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag >= HTML_TAG_H1 && c->tag <= HTML_TAG_H6) count++;
    }
    TEST_ASSERT(count == 6, "Six headings found");
    html_free(&parser);
    return 0;
}

int test_paragraphs_and_breaks() {
    const char *html = "<p>First</p><p>Second<br>Line</p>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Paragraphs parsed");
    int p_count = 0, br_count = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_P) p_count++;
        for (html_node_t *cc = c->children; cc; cc = cc->next_sibling) {
            if (cc->tag == HTML_TAG_BR) br_count++;
        }
    }
    TEST_ASSERT(p_count == 2, "Two paragraphs");
    TEST_ASSERT(br_count == 1, "One break");
    html_free(&parser);
    return 0;
}

int test_lists() {
    const char *html = "<ul><li>Item 1</li><li>Item 2</li></ul><ol><li>One</li></ol>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Lists parsed");
    int ul = 0, ol = 0, li = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_UL) ul++;
        if (c->tag == HTML_TAG_OL) ol++;
        for (html_node_t *cc = c->children; cc; cc = cc->next_sibling) {
            if (cc->tag == HTML_TAG_LI) li++;
        }
    }
    TEST_ASSERT(ul == 1, "One UL");
    TEST_ASSERT(ol == 1, "One OL");
    TEST_ASSERT(li == 3, "Three LIs");
    html_free(&parser);
    return 0;
}

int test_links() {
    const char *html = "<a href=\"http://example.com\">Link</a>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Link parsed");
    int found = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_A) {
            for (u32 i = 0; i < c->attr_count; i++) {
                if (c->attrs[i].type == HTML_ATTR_HREF) {
                    TEST_ASSERT(strncmp(c->attrs[i].value, "http://example.com", c->attrs[i].value_len) == 0, "Href correct");
                    found = 1;
                }
            }
        }
    }
    TEST_ASSERT(found, "Link with href found");
    html_free(&parser);
    return 0;
}

int test_formatting() {
    const char *html = "<b>Bold</b><strong>Strong</strong><i>Italic</i><em>Em</em>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Formatting parsed");
    int b = 0, s = 0, i = 0, e = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_B) b++;
        if (c->tag == HTML_TAG_STRONG) s++;
        if (c->tag == HTML_TAG_I) i++;
        if (c->tag == HTML_TAG_EM) e++;
    }
    TEST_ASSERT(b == 1 && s == 1 && i == 1 && e == 1, "All formatting tags");
    html_free(&parser);
    return 0;
}

int test_pre() {
    const char *html = "<pre>Line 1\nLine 2</pre>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Pre parsed");
    int found = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_PRE) found = 1;
    }
    TEST_ASSERT(found, "Pre tag found");
    html_free(&parser);
    return 0;
}

int test_entities() {
    char buf[256];
    const char *basic = "&lt;&gt;&amp;&quot;&apos;&nbsp;";
    html_decode_text(buf, sizeof(buf), basic, strlen(basic));
    TEST_ASSERT(strcmp(buf, "<>&\"' ") == 0, "Basic entities decoded");
    html_decode_text(buf, sizeof(buf), "&#65;&#x42;", 11);
    TEST_ASSERT(strcmp(buf, "AB") == 0, "Numeric entities decoded");
    return 0;
}

int test_script_style_ignored() {
    const char *html = "<script>alert('x')</script><style>body{}</style><p>Text</p>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Script/style ignored");
    int script = 0, style = 0, p = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_SCRIPT) script = 1;
        if (c->tag == HTML_TAG_STYLE) style = 1;
        if (c->tag == HTML_TAG_P) p = 1;
    }
    TEST_ASSERT(!script && !style && p, "Script/style not in tree, p is");
    html_free(&parser);
    return 0;
}

int test_hr() {
    const char *html = "<hr><hr/>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "HR parsed");
    int count = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_HR) count++;
    }
    TEST_ASSERT(count == 2, "Two HR elements");
    html_free(&parser);
    return 0;
}

int test_void_elements() {
    const char *html = "<br><hr><img>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Void elements parsed");
    int br = 0, hr = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_BR) br++;
        if (c->tag == HTML_TAG_HR) hr++;
    }
    TEST_ASSERT(br == 1 && hr == 1, "BR and HR recognized");
    html_free(&parser);
    return 0;
}

int test_nested_elements() {
    const char *html = "<div><p><b>Bold <i>and italic</i></b></p></div>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Nested elements parsed");
    int div = 0, p = 0, b = 0, i = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_DIV) {
            div = 1;
            for (html_node_t *cc = c->children; cc; cc = cc->next_sibling) {
                if (cc->tag == HTML_TAG_P) {
                    p = 1;
                    for (html_node_t *ccc = cc->children; ccc; ccc = ccc->next_sibling) {
                        if (ccc->tag == HTML_TAG_B) {
                            b = 1;
                            for (html_node_t *cccc = ccc->children; cccc; cccc = cccc->next_sibling) {
                                if (cccc->tag == HTML_TAG_I) i = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    TEST_ASSERT(div && p && b && i, "Nesting preserved");
    html_free(&parser);
    return 0;
}

int test_layout_basic() {
    layout_engine_t engine;
    layout_init(&engine, 800);
    const char *html = "<p>Hello World</p>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    layout_process_node(&engine, root);
    layout_finalize(&engine);
    TEST_ASSERT(engine.item_count > 0, "Layout items created");
    TEST_ASSERT(layout_get_height(&engine) > 0, "Layout has height");
    html_free(&parser);
    return 0;
}

int test_layout_wrapping() {
    layout_engine_t engine;
    layout_init(&engine, 200);
    const char *html = "<p>This is a very long line that should wrap multiple times because the width is small</p>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    layout_process_node(&engine, root);
    layout_finalize(&engine);
    TEST_ASSERT(engine.item_count > 1, "Text wrapped into multiple items");
    int max_y = 0;
    for (u32 i = 0; i < engine.item_count; i++) {
        if (engine.items[i].y > max_y) max_y = engine.items[i].y;
    }
    TEST_ASSERT(max_y > engine.line_height, "Text wrapped to multiple lines");
    html_free(&parser);
    return 0;
}

int test_layout_link() {
    layout_engine_t engine;
    layout_init(&engine, 800);
    const char *html = "<a href=\"http://test.com\">Click me</a>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    layout_process_node(&engine, root);
    layout_finalize(&engine);
    int link_found = 0;
    for (u32 i = 0; i < engine.item_count; i++) {
        if (engine.items[i].type == LAYOUT_ITEM_LINK) {
            link_found = 1;
            TEST_ASSERT(strncmp(engine.items[i].data.link.url, "http://test.com", engine.items[i].data.link.url_len) == 0, "Link URL preserved");
        }
    }
    TEST_ASSERT(link_found, "Link item created");
    html_free(&parser);
    return 0;
}

int test_layout_hr() {
    layout_engine_t engine;
    layout_init(&engine, 800);
    const char *html = "<p>Before</p><hr><p>After</p>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    layout_process_node(&engine, root);
    layout_finalize(&engine);
    int hr_found = 0;
    for (u32 i = 0; i < engine.item_count; i++) {
        if (engine.items[i].type == LAYOUT_ITEM_HR) hr_found = 1;
    }
    TEST_ASSERT(hr_found, "HR item created");
    html_free(&parser);
    return 0;
}

int test_layout_list() {
    layout_engine_t engine;
    layout_init(&engine, 800);
    const char *html = "<ul><li>Item 1</li><li>Item 2</li></ul>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    layout_process_node(&engine, root);
    layout_finalize(&engine);
    int bullet_count = 0;
    for (u32 i = 0; i < engine.item_count; i++) {
        if (engine.items[i].type == LAYOUT_ITEM_TEXT &&
            engine.items[i].data.text.len == 3 &&
            strncmp(engine.items[i].data.text.text, "\xE2\x80\xA2", 3) == 0) {
            bullet_count++;
        }
    }
    TEST_ASSERT(bullet_count == 2, "Two bullets created");
    html_free(&parser);
    return 0;
}

int test_layout_headings() {
    layout_engine_t engine;
    layout_init(&engine, 800);
    const char *html = "<h1>Big</h1><h6>Small</h6>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    layout_process_node(&engine, root);
    layout_finalize(&engine);
    int h1_found = 0, h6_found = 0;
    for (u32 i = 0; i < engine.item_count; i++) {
        if (engine.items[i].type == LAYOUT_ITEM_TEXT) {
            if (engine.items[i].data.text.heading_level == 1) h1_found = 1;
            if (engine.items[i].data.text.heading_level == 6) h6_found = 1;
        }
    }
    TEST_ASSERT(h1_found && h6_found, "Heading levels preserved");
    html_free(&parser);
    return 0;
}

int test_malformed_html() {
    const char *html = "<p>Unclosed<b>bold</p>text";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Malformed HTML doesn't crash");
    html_free(&parser);
    return 0;
}

int test_unclosed_tags() {
    const char *html = "<div><p>Text";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Unclosed tags handled");
    html_free(&parser);
    return 0;
}

int test_self_closing() {
    const char *html = "<br/><hr/><img src=\"x\"/>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Self-closing tags parsed");
    int br = 0, hr = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_BR) br++;
        if (c->tag == HTML_TAG_HR) hr++;
    }
    TEST_ASSERT(br == 1 && hr == 1, "Self-closing BR/HR recognized");
    html_free(&parser);
    return 0;
}

int test_comments() {
    const char *html = "<!-- comment --><p>Text</p><!-- another -->";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Comments skipped");
    int p = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_P) p = 1;
    }
    TEST_ASSERT(p, "Content after comment parsed");
    html_free(&parser);
    return 0;
}

int test_attributes() {
    const char *html = "<a href=\"link\" id=\"myid\" class=\"myclass\">X</a>";
    html_parser_t parser;
    html_parser_init(&parser, html, strlen(html));
    html_node_t *root = html_parse(&parser);
    TEST_ASSERT(root != 0, "Attributes parsed");
    int href = 0, id = 0, cls = 0;
    for (html_node_t *c = root->children; c; c = c->next_sibling) {
        if (c->tag == HTML_TAG_A) {
            for (u32 i = 0; i < c->attr_count; i++) {
                if (c->attrs[i].type == HTML_ATTR_HREF) href = 1;
                if (c->attrs[i].type == HTML_ATTR_ID) id = 1;
                if (c->attrs[i].type == HTML_ATTR_CLASS) cls = 1;
            }
        }
    }
    TEST_ASSERT(href && id && cls, "All attributes recognized");
    html_free(&parser);
    return 0;
}

int main() {
    int failed = 0;
    printf("Running HTML parser tests...\n\n");

    failed |= test_basic_parsing();
    failed |= test_headings();
    failed |= test_paragraphs_and_breaks();
    failed |= test_lists();
    failed |= test_links();
    failed |= test_formatting();
    failed |= test_pre();
    failed |= test_entities();
    failed |= test_script_style_ignored();
    failed |= test_hr();
    failed |= test_void_elements();
    failed |= test_nested_elements();
    failed |= test_malformed_html();
    failed |= test_unclosed_tags();
    failed |= test_self_closing();
    failed |= test_comments();
    failed |= test_attributes();

    printf("\nRunning Layout engine tests...\n\n");

    failed |= test_layout_basic();
    failed |= test_layout_wrapping();
    failed |= test_layout_link();
    failed |= test_layout_hr();
    failed |= test_layout_list();
    failed |= test_layout_headings();

    printf("\n========================\n");
    if (failed) {
        printf("SOME TESTS FAILED\n");
        return 1;
    } else {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
}
