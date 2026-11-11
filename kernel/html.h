#ifndef HTML_H
#define HTML_H

#include "nimbus.h"

#define HTML_MAX_DEPTH 32
#define HTML_MAX_ATTRS 16
#define HTML_MAX_TEXT_NODES 256
#define HTML_MAX_ELEMENTS 512

typedef enum {
    HTML_TAG_UNKNOWN = 0,
    HTML_TAG_HTML,
    HTML_TAG_HEAD,
    HTML_TAG_BODY,
    HTML_TAG_H1,
    HTML_TAG_H2,
    HTML_TAG_H3,
    HTML_TAG_H4,
    HTML_TAG_H5,
    HTML_TAG_H6,
    HTML_TAG_P,
    HTML_TAG_BR,
    HTML_TAG_HR,
    HTML_TAG_UL,
    HTML_TAG_OL,
    HTML_TAG_LI,
    HTML_TAG_A,
    HTML_TAG_B,
    HTML_TAG_STRONG,
    HTML_TAG_I,
    HTML_TAG_EM,
    HTML_TAG_PRE,
    HTML_TAG_SCRIPT,
    HTML_TAG_STYLE,
    HTML_TAG_SPAN,
    HTML_TAG_DIV,
} html_tag_t;

typedef enum {
    HTML_ATTR_NONE = 0,
    HTML_ATTR_HREF,
    HTML_ATTR_ID,
    HTML_ATTR_CLASS,
    HTML_ATTR_STYLE,
} html_attr_type_t;

typedef struct {
    html_attr_type_t type;
    const char *value;
    u32 value_len;
} html_attr_t;

typedef struct html_node {
    html_tag_t tag;
    struct html_node *parent;
    struct html_node *children;
    struct html_node *next_sibling;
    struct html_node *prev_sibling;
    html_attr_t attrs[HTML_MAX_ATTRS];
    u32 attr_count;
    const char *text_content;
    u32 text_len;
    int is_text_node;
} html_node_t;

typedef struct {
    html_node_t nodes[HTML_MAX_ELEMENTS];
    u32 node_count;
    html_node_t *root;
    const char *input;
    u32 input_len;
    u32 pos;
    int in_script;
    int in_style;
    int in_pre;
} html_parser_t;

void html_parser_init(html_parser_t *parser, const char *input, u32 len);
/* Input is a bounded byte slice and must outlive the tree. Text/attributes
 * remain raw slices; html_decode_text decodes them without reading past len.
 * Node exhaustion returns the partial tree; excess depth is flattened. */
html_node_t *html_parse(html_parser_t *parser);
void html_free(html_parser_t *parser);

const char *html_tag_name(html_tag_t tag);
html_tag_t html_tag_from_string(const char *str, u32 len);
int html_is_void_element(html_tag_t tag);
int html_is_block_element(html_tag_t tag);
int html_is_inline_element(html_tag_t tag);

/* NUL-terminated entity; consumed is zero on failure. Basic entities and
 * numeric values 1..255 use single-byte output (nbsp maps to space). */
char html_decode_entity(const char *str, u32 *consumed);
void html_decode_text(char *dst, u32 dst_size, const char *src, u32 src_len);

#endif
