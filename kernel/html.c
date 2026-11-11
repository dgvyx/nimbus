#include "html.h"

static const struct { const char *name; html_tag_t tag; } tag_table[] = {
    {"html", HTML_TAG_HTML}, {"head", HTML_TAG_HEAD}, {"body", HTML_TAG_BODY},
    {"h1", HTML_TAG_H1}, {"h2", HTML_TAG_H2}, {"h3", HTML_TAG_H3},
    {"h4", HTML_TAG_H4}, {"h5", HTML_TAG_H5}, {"h6", HTML_TAG_H6},
    {"p", HTML_TAG_P}, {"br", HTML_TAG_BR}, {"hr", HTML_TAG_HR},
    {"ul", HTML_TAG_UL}, {"ol", HTML_TAG_OL}, {"li", HTML_TAG_LI},
    {"a", HTML_TAG_A}, {"b", HTML_TAG_B}, {"strong", HTML_TAG_STRONG},
    {"i", HTML_TAG_I}, {"em", HTML_TAG_EM}, {"pre", HTML_TAG_PRE},
    {"script", HTML_TAG_SCRIPT}, {"style", HTML_TAG_STYLE},
    {"span", HTML_TAG_SPAN}, {"div", HTML_TAG_DIV}, {0, HTML_TAG_UNKNOWN}
};

static const struct { const char *name; html_attr_type_t type; } attr_table[] = {
    {"href", HTML_ATTR_HREF}, {"id", HTML_ATTR_ID},
    {"class", HTML_ATTR_CLASS}, {"style", HTML_ATTR_STYLE},
    {0, HTML_ATTR_NONE}
};

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
}

static int slice_equal_ci(const char *a, u32 len, const char *b) {
    u32 i = 0;
    while (b[i] && i < len && lower_ascii(a[i]) == b[i]) i++;
    return i == len && b[i] == 0;
}

static html_tag_t identify_tag(const char *name, u32 len) {
    for (u32 i = 0; tag_table[i].name; i++) {
        u32 n = 0;
        while (tag_table[i].name[n]) n++;
        if (n == len && slice_equal_ci(name, len, tag_table[i].name)) return tag_table[i].tag;
    }
    return HTML_TAG_UNKNOWN;
}

static html_attr_type_t identify_attr(const char *name, u32 len) {
    for (u32 i = 0; attr_table[i].name; i++) {
        u32 n = 0;
        while (attr_table[i].name[n]) n++;
        if (n == len && slice_equal_ci(name, len, attr_table[i].name)) return attr_table[i].type;
    }
    return HTML_ATTR_NONE;
}

static html_node_t *create_node(html_parser_t *parser, html_tag_t tag, int is_text) {
    if (parser->node_count >= HTML_MAX_ELEMENTS) return 0;
    html_node_t *node = &parser->nodes[parser->node_count++];
    node->tag = tag;
    node->parent = 0;
    node->children = 0;
    node->next_sibling = 0;
    node->prev_sibling = 0;
    node->attr_count = 0;
    node->text_content = 0;
    node->text_len = 0;
    node->is_text_node = is_text;
    return node;
}

static void append_child(html_node_t *parent, html_node_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    if (!parent->children) {
        parent->children = child;
        return;
    }
    html_node_t *last = parent->children;
    while (last->next_sibling) last = last->next_sibling;
    last->next_sibling = child;
    child->prev_sibling = last;
}

static void skip_space(html_parser_t *p) {
    while (p->pos < p->input_len && is_space(p->input[p->pos])) p->pos++;
}

static u32 read_name(html_parser_t *p, const char **name) {
    u32 start = p->pos;
    while (p->pos < p->input_len && name_char(p->input[p->pos])) p->pos++;
    *name = p->input + start;
    return p->pos - start;
}

static void parse_attributes(html_parser_t *p, html_node_t *node) {
    while (p->pos < p->input_len) {
        skip_space(p);
        if (p->pos >= p->input_len || p->input[p->pos] == '>') return;
        if (p->input[p->pos] == '/' && p->pos + 1 < p->input_len && p->input[p->pos + 1] == '>') return;

        const char *name;
        u32 name_len = read_name(p, &name);
        if (!name_len) { p->pos++; continue; }
        skip_space(p);

        const char *value = p->input + p->pos;
        u32 value_len = 0;
        if (p->pos < p->input_len && p->input[p->pos] == '=') {
            p->pos++;
            skip_space(p);
            if (p->pos < p->input_len && (p->input[p->pos] == '\'' || p->input[p->pos] == '"')) {
                char quote = p->input[p->pos++];
                u32 start = p->pos;
                while (p->pos < p->input_len && p->input[p->pos] != quote) p->pos++;
                value = p->input + start;
                value_len = p->pos - start;
                if (p->pos < p->input_len) p->pos++;
            } else {
                u32 start = p->pos;
                while (p->pos < p->input_len && !is_space(p->input[p->pos]) &&
                       p->input[p->pos] != '>' &&
                       !(p->input[p->pos] == '/' && p->pos + 1 < p->input_len && p->input[p->pos + 1] == '>')) p->pos++;
                value = p->input + start;
                value_len = p->pos - start;
            }
        }

        html_attr_type_t type = identify_attr(name, name_len);
        if (type != HTML_ATTR_NONE && node->attr_count < HTML_MAX_ATTRS) {
            html_attr_t *attr = &node->attrs[node->attr_count++];
            attr->type = type;
            attr->value = value;
            attr->value_len = value_len;
        }
    }
}

static void skip_to_tag_end(html_parser_t *p) {
    char quote = 0;
    while (p->pos < p->input_len) {
        char c = p->input[p->pos];
        if (!quote && c == '>') break;
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '\'' || c == '"') quote = c;
        p->pos++;
    }
    if (p->pos < p->input_len) p->pos++;
}

static void skip_comment_or_declaration(html_parser_t *p) {
    if (p->pos + 2 < p->input_len && p->input[p->pos] == '!' &&
        p->input[p->pos + 1] == '-' && p->input[p->pos + 2] == '-') {
        p->pos += 3;
        while (p->pos + 2 < p->input_len) {
            if (p->input[p->pos] == '-' && p->input[p->pos + 1] == '-' && p->input[p->pos + 2] == '>') {
                p->pos += 3;
                return;
            }
            p->pos++;
        }
        p->pos = p->input_len;
        return;
    }
    skip_to_tag_end(p);
}

static int starts_closing_tag(html_parser_t *p, const char *tag, u32 len) {
    if (p->pos + len + 2 > p->input_len || p->input[p->pos] != '<' || p->input[p->pos + 1] != '/') return 0;
    for (u32 i = 0; i < len; i++)
        if (lower_ascii(p->input[p->pos + 2 + i]) != lower_ascii(tag[i])) return 0;
    u32 end = p->pos + 2 + len;
    return end < p->input_len && (is_space(p->input[end]) || p->input[end] == '>' || p->input[end] == '/');
}

static void skip_raw_element(html_parser_t *p, const char *tag, u32 len) {
    while (p->pos < p->input_len) {
        if (starts_closing_tag(p, tag, len)) {
            p->pos += 2 + len;
            skip_to_tag_end(p);
            return;
        }
        p->pos++;
    }
}

static int is_heading(html_tag_t tag) {
    return tag >= HTML_TAG_H1 && tag <= HTML_TAG_H6;
}

static void close_to_tag(html_node_t **stack, u32 *depth, html_tag_t tag) {
    for (u32 i = *depth; i > 1; i--) {
        if (stack[i - 1]->tag == tag) {
            *depth = i - 1;
            return;
        }
    }
}

void html_parser_init(html_parser_t *parser, const char *input, u32 len) {
    parser->input = input;
    parser->input_len = input ? len : 0;
    parser->pos = 0;
    parser->node_count = 0;
    parser->root = 0;
    parser->in_script = 0;
    parser->in_style = 0;
    parser->in_pre = 0;
}

html_node_t *html_parse(html_parser_t *parser) {
    html_node_t *stack[HTML_MAX_DEPTH];
    u32 depth = 1;
    html_node_t *root = create_node(parser, HTML_TAG_HTML, 0);
    if (!root) return 0;
    parser->root = root;
    stack[0] = root;
    html_node_t *body = 0;

    while (parser->pos < parser->input_len && parser->node_count < HTML_MAX_ELEMENTS) {
        if (parser->input[parser->pos] != '<') {
            u32 start = parser->pos;
            while (parser->pos < parser->input_len && parser->input[parser->pos] != '<') parser->pos++;
            if (parser->pos > start) {
                html_node_t *text = create_node(parser, HTML_TAG_UNKNOWN, 1);
                if (text) {
                    text->text_content = parser->input + start;
                    text->text_len = parser->pos - start;
                    append_child(stack[depth - 1], text);
                }
            }
            continue;
        }

        if (parser->pos + 1 == parser->input_len ||
            !(name_char(parser->input[parser->pos + 1]) ||
              parser->input[parser->pos + 1] == '/' ||
              parser->input[parser->pos + 1] == '!' ||
              parser->input[parser->pos + 1] == '?')) {
            html_node_t *text = create_node(parser, HTML_TAG_UNKNOWN, 1);
            if (text) {
                text->text_content = parser->input + parser->pos;
                text->text_len = 1;
                append_child(stack[depth - 1], text);
            }
            parser->pos++;
            continue;
        }
        parser->pos++;
        if (parser->input[parser->pos] == '!') {
            skip_comment_or_declaration(parser);
            continue;
        }
        if (parser->input[parser->pos] == '?') {
            skip_to_tag_end(parser);
            continue;
        }

        int closing = 0;
        if (parser->input[parser->pos] == '/') { closing = 1; parser->pos++; }
        const char *name;
        u32 name_len = read_name(parser, &name);
        if (!name_len) {
            if (parser->pos < parser->input_len && parser->input[parser->pos] == '<') continue;
            skip_to_tag_end(parser);
            continue;
        }
        html_tag_t tag = identify_tag(name, name_len);
        if (closing) {
            skip_to_tag_end(parser);
            if (tag != HTML_TAG_UNKNOWN) close_to_tag(stack, &depth, tag);
            else {
                for (u32 i = depth; i > 1; i--) {
                    if (stack[i - 1]->tag == HTML_TAG_UNKNOWN) { depth = i - 1; break; }
                }
            }
            continue;
        }

        if (tag == HTML_TAG_SCRIPT || tag == HTML_TAG_STYLE) {
            parser->in_script = tag == HTML_TAG_SCRIPT;
            parser->in_style = tag == HTML_TAG_STYLE;
            skip_to_tag_end(parser);
            skip_raw_element(parser, name, name_len);
            parser->in_script = parser->in_style = 0;
            continue;
        }

        html_node_t *parent = stack[depth - 1];
        if (html_is_block_element(tag)) close_to_tag(stack, &depth, HTML_TAG_P);
        if (tag == HTML_TAG_LI) {
            for (u32 i = depth; i > 1; i--) {
                html_tag_t t = stack[i - 1]->tag;
                if (t == HTML_TAG_UL || t == HTML_TAG_OL) break;
                if (t == HTML_TAG_LI) { depth = i - 1; break; }
            }
        }
        if (is_heading(tag)) {
            for (html_tag_t h = HTML_TAG_H1; h <= HTML_TAG_H6; h++) close_to_tag(stack, &depth, h);
        }
        parent = stack[depth - 1];

        html_node_t *node = create_node(parser, tag, 0);
        if (!node) break;
        parse_attributes(parser, node);
        skip_space(parser);
        int self_closing = 0;
        if (parser->pos < parser->input_len && parser->input[parser->pos] == '/') {
            self_closing = 1;
            parser->pos++;
            skip_space(parser);
        }
        if (parser->pos < parser->input_len && parser->input[parser->pos] == '>') parser->pos++;
        else if (parser->pos < parser->input_len) skip_to_tag_end(parser);
        append_child(parent, node);
        if (tag == HTML_TAG_BODY) body = node;
        if (!self_closing && !html_is_void_element(tag) && depth < HTML_MAX_DEPTH) stack[depth++] = node;
    }

    return body ? body : root;
}

void html_free(html_parser_t *parser) { (void)parser; }

const char *html_tag_name(html_tag_t tag) {
    for (u32 i = 0; tag_table[i].name; i++) if (tag_table[i].tag == tag) return tag_table[i].name;
    return "unknown";
}

html_tag_t html_tag_from_string(const char *str, u32 len) { return identify_tag(str, len); }

int html_is_void_element(html_tag_t tag) { return tag == HTML_TAG_BR || tag == HTML_TAG_HR; }

int html_is_block_element(html_tag_t tag) {
    return tag == HTML_TAG_P || tag == HTML_TAG_DIV || is_heading(tag) ||
           tag == HTML_TAG_UL || tag == HTML_TAG_OL || tag == HTML_TAG_LI ||
           tag == HTML_TAG_PRE || tag == HTML_TAG_HR;
}

int html_is_inline_element(html_tag_t tag) {
    return tag == HTML_TAG_A || tag == HTML_TAG_B || tag == HTML_TAG_STRONG ||
           tag == HTML_TAG_I || tag == HTML_TAG_EM || tag == HTML_TAG_SPAN;
}

static const struct { const char *name; char value; } entities[] = {
    {"lt", '<'}, {"gt", '>'}, {"amp", '&'}, {"quot", '"'},
    {"apos", '\''}, {"nbsp", ' '}, {"copy", (char)0xA9},
    {"reg", (char)0xAE}, {"times", (char)0xD7}, {"divide", (char)0xF7}, {0, 0}
};

char html_decode_entity(const char *str, u32 *consumed) {
    if (!consumed) return 0;
    *consumed = 0;
    if (!str || str[0] != '&') return 0;
    u32 len = 0;
    while (len < 32 && str[len + 1] && str[len + 1] != ';' &&
           !is_space(str[len + 1]) && str[len + 1] != '<' && str[len + 1] != '&') len++;
    if (len == 0) return 0;
    u32 end = len + 1;
    int has_semicolon = str[end] == ';';
    if (!has_semicolon && !(str[end] == 0 || is_space(str[end]) || str[end] == '<' || str[end] == '&')) return 0;

    if (str[1] == '#') {
        u32 i = 2, base = 10, value = 0;
        if (i < end && (str[i] == 'x' || str[i] == 'X')) { base = 16; i++; }
        if (i >= end) return 0;
        for (; i < end; i++) {
            char c = str[i];
            u32 digit;
            if (c >= '0' && c <= '9') digit = (u32)(c - '0');
            else if (base == 16 && c >= 'a' && c <= 'f') digit = (u32)(c - 'a' + 10);
            else if (base == 16 && c >= 'A' && c <= 'F') digit = (u32)(c - 'A' + 10);
            else return 0;
            if (digit >= base || value > 0x10FFFFU / base) return 0;
            value = value * base + digit;
        }
        if (value == 0 || value > 255) return 0;
        *consumed = has_semicolon ? end + 1 : end;
        return (char)value;
    }

    for (u32 i = 0; entities[i].name; i++) {
        u32 n = 0;
        while (entities[i].name[n]) n++;
        if (n != len) continue;
        int match = 1;
        for (u32 j = 0; j < n; j++) if (str[j + 1] != entities[i].name[j]) { match = 0; break; }
        if (match) {
            *consumed = has_semicolon ? end + 1 : end;
            return entities[i].value;
        }
    }
    return 0;
}

void html_decode_text(char *dst, u32 dst_size, const char *src, u32 src_len) {
    if (!dst || dst_size == 0) return;
    u32 out = 0, in = 0;
    while (src && in < src_len && src[in] && out + 1 < dst_size) {
        if (src[in] == '&') {
            char entity[35];
            entity[0] = '&';
            u32 available = src_len - in;
            if (available > sizeof(entity) - 1) available = sizeof(entity) - 1;
            for (u32 i = 1; i < available; i++) entity[i] = src[in + i];
            entity[available] = 0;
            u32 consumed = 0;
            char decoded = html_decode_entity(entity, &consumed);
            if (consumed && consumed <= available && decoded) {
                dst[out++] = decoded;
                in += consumed;
                continue;
            }
        }
        dst[out++] = src[in++];
    }
    dst[out] = 0;
}
