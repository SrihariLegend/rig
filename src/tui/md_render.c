#include "md_render.h"
#include "md4c/md4c.h"
#include "ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {
    LineStore *ls;
    LineType default_type;

    int in_code_block;
    int in_heading;
    int heading_level;
    int in_list_item;
    int list_depth;
    int in_code_span;
    int in_bold;
    int in_italic;
    int in_strikethrough;
    int in_blockquote;
    int in_link;

    int list_ordered[8];
    int list_counter[8];

    char *line_buf;
    int line_len;
    int line_cap;

    Span *spans;
    int span_count;
    int span_cap;

    char code_lang[32];
} MdState;

static void buf_ensure(MdState *st, int need) {
    if (st->line_len + need >= st->line_cap) {
        int new_cap = st->line_cap ? st->line_cap * 2 : 256;
        while (new_cap < st->line_len + need + 1) new_cap *= 2;
        char *nb = realloc(st->line_buf, new_cap);
        if (!nb) return;
        st->line_buf = nb;
        st->line_cap = new_cap;
    }
}

static void buf_append(MdState *st, const char *text, int len) {
    buf_ensure(st, len);
    memcpy(st->line_buf + st->line_len, text, len);
    st->line_len += len;
    st->line_buf[st->line_len] = '\0';
}

static void buf_clear(MdState *st) {
    st->line_len = 0;
    if (st->line_buf) st->line_buf[0] = '\0';
    st->span_count = 0;
}

static SpanFlags current_flags(MdState *st) {
    SpanFlags f = SPAN_PLAIN;
    if (st->in_bold) f |= SPAN_BOLD;
    if (st->in_italic) f |= SPAN_ITALIC;
    if (st->in_code_span) f |= SPAN_CODE;
    if (st->in_strikethrough) f |= SPAN_STRIKE;
    return f;
}

static void push_span(MdState *st, const char *text, int len, SpanFlags flags) {
    if (len <= 0) return;
    if (st->span_count >= st->span_cap) {
        int nc = st->span_cap ? st->span_cap * 2 : 8;
        Span *ns = realloc(st->spans, (size_t)nc * sizeof(Span));
        if (!ns) return;
        st->spans = ns;
        st->span_cap = nc;
    }
    /* text points into line_buf — store offset, resolve to pointer after flush */
    int offset = st->line_len;
    buf_append(st, text, len);
    st->spans[st->span_count++] = (Span){
        .text = (const char *)(intptr_t)offset,
        .len = len,
        .flags = flags,
    };
}

static void flush_line(MdState *st, LineType type, int indent, int heading_level) {
    if (st->ls->count >= st->ls->capacity) {
        int new_cap = st->ls->capacity * 2;
        StoreLine *new_lines = realloc(st->ls->lines, (size_t)new_cap * sizeof(StoreLine));
        if (!new_lines) return;
        st->ls->lines = new_lines;
        st->ls->capacity = new_cap;
    }

    StoreLine *line = &st->ls->lines[st->ls->count];
    memset(line, 0, sizeof(StoreLine));
    line->msg_index = st->ls->current_msg;
    line->type = type;
    line->indent = (uint16_t)indent;
    line->heading_level = (uint16_t)heading_level;
    line->raw_text = st->line_buf && st->line_len > 0 ? strdup(st->line_buf) : strdup("");
    line->wrap_count = 1;

    if (line->raw_text) {
        int ew = st->ls->content_width - indent;
        if (ew < 10) ew = 10;
        int vis = unicode_display_width(line->raw_text);
        if (vis > ew) {
            line->wrap_count = (uint16_t)((vis + ew - 1) / ew);
        }
    }

    /* Copy spans — resolve offsets to pointers into raw_text */
    if (st->span_count > 0 && line->raw_text) {
        line->spans = calloc(st->span_count, sizeof(Span));
        if (line->spans) {
            line->span_count = st->span_count;
            for (int i = 0; i < st->span_count; i++) {
                int offset = (int)(intptr_t)st->spans[i].text;
                line->spans[i].text = line->raw_text + offset;
                line->spans[i].len = st->spans[i].len;
                line->spans[i].flags = st->spans[i].flags;
            }
        }
    }

    st->ls->total_screen_rows += line->wrap_count;
    st->ls->count++;
    buf_clear(st);
}

static int md_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    MdState *st = userdata;

    switch (type) {
    case MD_BLOCK_H: {
        MD_BLOCK_H_DETAIL *h = detail;
        st->in_heading = 1;
        st->heading_level = h->level;
        buf_clear(st);
        break;
    }
    case MD_BLOCK_CODE: {
        MD_BLOCK_CODE_DETAIL *cd = detail;
        st->in_code_block = 1;
        st->code_lang[0] = '\0';
        if (cd->lang.text && cd->lang.size > 0) {
            int len = (int)cd->lang.size < 31 ? (int)cd->lang.size : 31;
            memcpy(st->code_lang, cd->lang.text, len);
            st->code_lang[len] = '\0';
            buf_append(st, st->code_lang, len);
            flush_line(st, LINE_CODE_LANG, 4, 0);
        }
        break;
    }
    case MD_BLOCK_UL:
        if (st->list_depth < 8) {
            st->list_ordered[st->list_depth] = 0;
            st->list_counter[st->list_depth] = 0;
        }
        st->list_depth++;
        break;
    case MD_BLOCK_OL:
        if (st->list_depth < 8) {
            st->list_ordered[st->list_depth] = 1;
            st->list_counter[st->list_depth] = 0;
        }
        st->list_depth++;
        break;
    case MD_BLOCK_LI:
        st->in_list_item = 1;
        if (st->list_depth > 0 && st->list_depth <= 8) {
            st->list_counter[st->list_depth - 1]++;
        }
        buf_clear(st);
        break;
    case MD_BLOCK_QUOTE:
        st->in_blockquote = 1;
        break;
    case MD_BLOCK_P:
        buf_clear(st);
        break;
    default:
        break;
    }
    return 0;
}

static int md_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    (void)detail;
    MdState *st = userdata;

    switch (type) {
    case MD_BLOCK_H:
        flush_line(st, LINE_HEADING, 0, st->heading_level);
        st->in_heading = 0;
        break;
    case MD_BLOCK_CODE:
        st->in_code_block = 0;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        st->list_depth--;
        break;
    case MD_BLOCK_LI:
        if (st->line_len > 0) {
            int indent = 2 + (st->list_depth - 1) * 4;
            int num = 0;
            if (st->list_depth > 0 && st->list_depth <= 8 &&
                st->list_ordered[st->list_depth - 1]) {
                num = st->list_counter[st->list_depth - 1];
            }
            flush_line(st, LINE_LIST_ITEM, indent, num);
        }
        st->in_list_item = 0;
        break;
    case MD_BLOCK_QUOTE:
        if (st->line_len > 0) {
            flush_line(st, LINE_BLOCKQUOTE, 4, 0);
        }
        st->in_blockquote = 0;
        break;
    case MD_BLOCK_P:
        if (st->line_len > 0) {
            LineType lt = st->default_type;
            if (st->in_blockquote) lt = LINE_BLOCKQUOTE;
            flush_line(st, lt, 0, 0);
        }
        /* blank line after paragraphs */
        {
            StoreLine *blank = NULL;
            if (st->ls->count < st->ls->capacity) {
                blank = &st->ls->lines[st->ls->count];
            } else {
                int new_cap = st->ls->capacity * 2;
                StoreLine *nl = realloc(st->ls->lines, (size_t)new_cap * sizeof(StoreLine));
                if (nl) { st->ls->lines = nl; st->ls->capacity = new_cap; blank = &st->ls->lines[st->ls->count]; }
            }
            if (blank) {
                memset(blank, 0, sizeof(StoreLine));
                blank->type = LINE_BLANK;
                blank->raw_text = strdup("");
                blank->wrap_count = 1;
                blank->msg_index = st->ls->current_msg;
                st->ls->total_screen_rows++;
                st->ls->count++;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static int md_enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
    MdState *st = userdata;

    switch (type) {
    case MD_SPAN_STRONG: st->in_bold = 1; break;
    case MD_SPAN_EM:     st->in_italic = 1; break;
    case MD_SPAN_CODE:   st->in_code_span = 1; break;
    case MD_SPAN_DEL:    st->in_strikethrough = 1; break;
    case MD_SPAN_A:      st->in_link = 1; break;
    default: break;
    }
    (void)detail;
    return 0;
}

static int md_leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
    MdState *st = userdata;

    switch (type) {
    case MD_SPAN_STRONG: st->in_bold = 0; break;
    case MD_SPAN_EM:     st->in_italic = 0; break;
    case MD_SPAN_CODE:   st->in_code_span = 0; break;
    case MD_SPAN_DEL:    st->in_strikethrough = 0; break;
    case MD_SPAN_A: {
        st->in_link = 0;
        MD_SPAN_A_DETAIL *a = detail;
        if (a && a->href.text && a->href.size > 0) {
            buf_append(st, " (", 2);
            buf_append(st, a->href.text, (int)a->href.size);
            buf_append(st, ")", 1);
        }
        break;
    }
    default: break;
    }
    return 0;
}

static int md_on_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                      void *userdata) {
    MdState *st = userdata;

    if (st->in_code_block) {
        /* Each line of code block text becomes its own StoreLine */
        MD_SIZE start = 0;
        for (MD_SIZE i = 0; i <= size; i++) {
            if (i == size || text[i] == '\n') {
                if (i > start) {
                    buf_append(st, text + start, (int)(i - start));
                }
                if (i < size) {
                    flush_line(st, LINE_CODE, 4, 0);
                }
                start = i + 1;
            }
        }
        return 0;
    }

    if (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR) {
        if (st->line_len > 0) {
            LineType lt = st->default_type;
            if (st->in_heading) lt = LINE_HEADING;
            else if (st->in_list_item) lt = LINE_LIST_ITEM;
            else if (st->in_blockquote) lt = LINE_BLOCKQUOTE;
            flush_line(st, lt, 0, st->heading_level);
        }
        return 0;
    }

    push_span(st, text, (int)size, current_flags(st));
    return 0;
}

void md_render_to_linestore(LineStore *ls, const char *markdown, int len, LineType default_type) {
    if (!ls || !markdown || len <= 0) return;

    MdState st = {
        .ls = ls,
        .default_type = default_type,
    };
    st.line_cap = 256;
    st.line_buf = calloc(st.line_cap, 1);

    MD_PARSER parser = {
        .abi_version = 0,
        .flags = MD_FLAG_STRIKETHROUGH | MD_FLAG_TABLES | MD_FLAG_NOHTML,
        .enter_block = md_enter_block,
        .leave_block = md_leave_block,
        .enter_span  = md_enter_span,
        .leave_span  = md_leave_span,
        .text        = md_on_text,
        .debug_log   = NULL,
        .syntax      = NULL,
    };

    md_parse(markdown, (MD_SIZE)len, &parser, &st);

    /* Flush any remaining buffered text */
    if (st.line_len > 0) {
        flush_line(&st, default_type, 0, 0);
    }

    free(st.line_buf);
    free(st.spans);
}
