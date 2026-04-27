#include "linestore.h"
#include "md_render.h"
#include "ansi.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define INITIAL_CAP 256
#define MAX_WRAP 10000

static int compute_wrap_count_indent(const char *text, int content_width, int indent) {
    int avail = content_width - indent;
    if (avail < 10) avail = 10;
    if (!text || !text[0] || avail <= 0) return 1;
    int text_len = (int)strlen(text);
    int vis_total = unicode_display_width(text);
    if (vis_total <= avail) return 1;

    int rows = 0;
    int pos = 0;
    while (pos < text_len && rows < MAX_WRAP) {
        int vis = 0;
        int last_space = -1;
        int row_end = pos;
        while (row_end < text_len && vis < avail) {
            if (text[row_end] == ' ') last_space = row_end;
            unsigned char c = (unsigned char)text[row_end];
            int bytes = 1;
            if (c >= 0xF0) bytes = 4;
            else if (c >= 0xE0) bytes = 3;
            else if (c >= 0xC0) bytes = 2;
            row_end += bytes;
            vis++;
        }
        if (row_end < text_len && last_space > pos) {
            row_end = last_space + 1;
        }
        rows++;
        pos = row_end;
        while (pos < text_len && text[pos] == ' ') pos++;
    }
    return rows > 0 ? rows : 1;
}

static int compute_wrap_count(const char *text, int content_width) {
    return compute_wrap_count_indent(text, content_width, 0);
}

int linestore_compute_wrap(const char *text, int content_width, int indent) {
    return compute_wrap_count_indent(text, content_width, indent);
}

#define SPANS_GROW(spans, count, cap) do { \
    if ((count) >= (cap)) { \
        int _nc = (cap) * 2; \
        Span *_ns = realloc((spans), (size_t)_nc * sizeof(Span)); \
        if (!_ns) goto done; \
        (spans) = _ns; \
        (cap) = _nc; \
    } \
} while(0)

LineStore *linestore_create(void) {
    LineStore *ls = calloc(1, sizeof(LineStore));
    if (!ls) return NULL;
    ls->capacity = INITIAL_CAP;
    ls->lines = calloc(INITIAL_CAP, sizeof(StoreLine));
    ls->content_width = 76;
    return ls;
}

void linestore_free(LineStore *ls) {
    if (!ls) return;
    for (int i = 0; i < ls->count; i++) {
        free(ls->lines[i].raw_text);
        free(ls->lines[i].spans);
    }
    free(ls->lines);
    free(ls->stream_buf);
    free(ls);
}

void linestore_clear(LineStore *ls) {
    if (!ls) return;
    for (int i = 0; i < ls->count; i++) {
        free(ls->lines[i].raw_text);
        free(ls->lines[i].spans);
    }
    ls->count = 0;
    ls->total_screen_rows = 0;
    ls->current_msg = 0;
    ls->in_code_block = false;
    ls->stream_len = 0;
    ls->stream_start_idx = 0;
    ls->stream_start_rows = 0;
    ls->needs_scroll_reset = false;
}

static StoreLine *linestore_alloc_line(LineStore *ls) {
    if (ls->count >= ls->capacity) {
        int new_cap = ls->capacity * 2;
        StoreLine *new_lines = realloc(ls->lines, (size_t)new_cap * sizeof(StoreLine));
        if (!new_lines) return NULL;
        ls->lines = new_lines;
        ls->capacity = new_cap;
    }
    StoreLine *line = &ls->lines[ls->count];
    memset(line, 0, sizeof(StoreLine));
    line->msg_index = ls->current_msg;
    ls->count++;
    return line;
}

static Span make_span(const char *text, int len, SpanFlags flags) {
    return (Span){ .text = text, .len = len, .flags = flags };
}

void linestore_parse_inline_spans(StoreLine *line) {
    const char *src = line->raw_text;
    if (!src) return;
    int src_len = (int)strlen(src);

    int span_cap = 8;
    Span *spans = calloc(span_cap, sizeof(Span));
    int span_count = 0;

    int i = 0;
    int seg_start = 0;

    while (i < src_len) {
        if (src[i] == '`') {
            if (i > seg_start) {
                SPANS_GROW(spans, span_count, span_cap);
                spans[span_count++] = make_span(src + seg_start, i - seg_start, SPAN_PLAIN);
            }
            int end = i + 1;
            while (end < src_len && src[end] != '`') end++;
            if (end < src_len) {
                SPANS_GROW(spans, span_count, span_cap);
                spans[span_count++] = make_span(src + i + 1, end - i - 1, SPAN_CODE);
                i = end + 1;
            } else {
                i++;
            }
            seg_start = i;
            continue;
        }

        if (i + 1 < src_len && src[i] == '*' && src[i + 1] == '*') {
            if (i > seg_start) {
                SPANS_GROW(spans, span_count, span_cap);
                spans[span_count++] = make_span(src + seg_start, i - seg_start, SPAN_PLAIN);
            }
            int end = i + 2;
            while (end + 1 < src_len && !(src[end] == '*' && src[end + 1] == '*')) end++;
            if (end + 1 < src_len) {
                SPANS_GROW(spans, span_count, span_cap);
                spans[span_count++] = make_span(src + i + 2, end - i - 2, SPAN_BOLD);
                i = end + 2;
            } else {
                i += 2;
            }
            seg_start = i;
            continue;
        }

        if (src[i] == '*' && !(i + 1 < src_len && src[i + 1] == '*')) {
            if (i > seg_start) {
                SPANS_GROW(spans, span_count, span_cap);
                spans[span_count++] = make_span(src + seg_start, i - seg_start, SPAN_PLAIN);
            }
            int end = i + 1;
            while (end < src_len && src[end] != '*') end++;
            if (end < src_len) {
                SPANS_GROW(spans, span_count, span_cap);
                spans[span_count++] = make_span(src + i + 1, end - i - 1, SPAN_ITALIC);
                i = end + 1;
            } else {
                i++;
            }
            seg_start = i;
            continue;
        }

        i++;
    }

    if (seg_start < src_len) {
        SPANS_GROW(spans, span_count, span_cap);
        spans[span_count++] = make_span(src + seg_start, src_len - seg_start, SPAN_PLAIN);
    }

done:
    line->spans = spans;
    line->span_count = span_count;
}

void linestore_set_width(LineStore *ls, int content_width) {
    if (content_width < 20) content_width = 20;
    if (content_width > 500) content_width = 500;
    ls->content_width = content_width;
}

void linestore_reflow(LineStore *ls) {
    LOG_INFO("reflow: content_width=%d lines=%d", ls->content_width, ls->count);
    ls->total_screen_rows = 0;
    for (int i = 0; i < ls->count; i++) {
        StoreLine *line = &ls->lines[i];
        int effective_width = ls->content_width - 4 - (line->indent & 0x7FFF);
        if (effective_width < 10) effective_width = 10;
        line->wrap_count = compute_wrap_count(line->raw_text, effective_width);
        ls->total_screen_rows += line->wrap_count;
    }
}

void linestore_add_blank(LineStore *ls) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_BLANK;
    line->raw_text = strdup("");
    line->wrap_count = 1;
    ls->total_screen_rows++;
}

void linestore_add_system(LineStore *ls, const char *text) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_SYSTEM;
    line->raw_text = strdup(text ? text : "");
    line->wrap_count = 1;
    ls->total_screen_rows++;
}

void linestore_add_user_text(LineStore *ls, const char *text) {
    if (!text) return;
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_USER_TEXT;
    line->raw_text = strdup(text);
    int ew = ls->content_width - 4 - 2;
    if (ew < 10) ew = 10;
    line->wrap_count = compute_wrap_count(line->raw_text, ew);
    ls->total_screen_rows += line->wrap_count;
}

void linestore_begin_message(LineStore *ls, uint32_t msg_index) {
    ls->current_msg = msg_index;
    ls->in_code_block = false;
    ls->code_lang[0] = '\0';
    ls->stream_start_idx = ls->count;
    ls->stream_start_rows = ls->total_screen_rows;
    ls->stream_len = 0;
    if (ls->stream_buf) ls->stream_buf[0] = '\0';
    LOG_INFO("begin_message: msg=%u bookmark=%d count=%d", msg_index, ls->stream_start_idx, ls->count);
}

static void stream_buf_append(LineStore *ls, const char *data, int len) {
    if (ls->stream_len + len >= ls->stream_cap) {
        int new_cap = ls->stream_cap ? ls->stream_cap * 2 : 256;
        while (new_cap < ls->stream_len + len + 1) new_cap *= 2;
        char *new_buf = realloc(ls->stream_buf, new_cap);
        if (!new_buf) return;
        ls->stream_buf = new_buf;
        ls->stream_cap = new_cap;
    }
    memcpy(ls->stream_buf + ls->stream_len, data, len);
    ls->stream_len += len;
    ls->stream_buf[ls->stream_len] = '\0';
}

void linestore_append_assistant_text(LineStore *ls, const char *text) {
    if (!text) return;
    int len = (int)strlen(text);
    stream_buf_append(ls, text, len);

    /* Truncate back to bookmark — wipe all streaming lines for this msg */
    while (ls->count > ls->stream_start_idx) {
        ls->count--;
        ls->total_screen_rows -= ls->lines[ls->count].wrap_count;
        free(ls->lines[ls->count].raw_text);
        free(ls->lines[ls->count].spans);
        ls->lines[ls->count].raw_text = NULL;
        ls->lines[ls->count].spans = NULL;
    }

    /* Re-add from stream_buf as raw lines */
    const char *p = ls->stream_buf;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int line_len = nl ? (int)(nl - p) : (int)strlen(p);

        StoreLine *line = linestore_alloc_line(ls);
        if (!line) return;
        line->type = LINE_ASSISTANT_TEXT;
        line->raw_text = line_len > 0 ? strndup(p, line_len) : strdup("");
        int ew = ls->content_width - 4;
        if (ew < 10) ew = 10;
        line->wrap_count = compute_wrap_count(line->raw_text, ew);
        ls->total_screen_rows += line->wrap_count;

        if (nl) p = nl + 1;
        else break;
    }
}

void linestore_flush_stream(LineStore *ls) {
    if (ls->stream_len <= 0) return;

    LOG_INFO("flush_stream: len=%d bookmark=%d count=%d",
             ls->stream_len, ls->stream_start_idx, ls->count);

    /* Truncate back to bookmark */
    while (ls->count > ls->stream_start_idx) {
        ls->count--;
        ls->total_screen_rows -= ls->lines[ls->count].wrap_count;
        free(ls->lines[ls->count].raw_text);
        free(ls->lines[ls->count].spans);
        ls->lines[ls->count].raw_text = NULL;
        ls->lines[ls->count].spans = NULL;
    }

    /* Strip leading whitespace per line to prevent false code blocks.
     * Models sometimes indent tables/lists with 4+ spaces. */
    {
        char *src = ls->stream_buf;
        char *dst = ls->stream_buf;
        bool at_line_start = true;
        for (int i = 0; i < ls->stream_len; i++) {
            if (at_line_start && (src[i] == ' ' || src[i] == '\t')) {
                continue;
            }
            at_line_start = (src[i] == '\n');
            *dst++ = src[i];
        }
        *dst = '\0';
        ls->stream_len = (int)(dst - ls->stream_buf);
    }

    /* Parse full text through md4c */
    int before = ls->count;
    md_render_to_linestore(ls, ls->stream_buf, ls->stream_len, LINE_ASSISTANT_TEXT);
    LOG_INFO("flush_stream: md4c added %d lines", ls->count - before);

    ls->stream_len = 0;
    if (ls->stream_buf) ls->stream_buf[0] = '\0';
    ls->needs_scroll_reset = true;
}

void linestore_add_tool_start(LineStore *ls, const char *name, const char *args) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_TOOL_START;
    int name_len = name ? (int)strlen(name) : 0;
    int args_len = args ? (int)strlen(args) : 0;
    int total = name_len + args_len + 8;
    line->raw_text = malloc(total);
    if (!line->raw_text) { ls->count--; return; }
    snprintf(line->raw_text, total, "%s %s", name ? name : "", args ? args : "");
    line->indent = 2;
    line->wrap_count = 1;
    ls->total_screen_rows++;
}

void linestore_add_tool_output(LineStore *ls, const char *text) {
    if (!text) return;
    const char *p = text;
    int lines_added = 0;
    while (*p && lines_added < 20) {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len > 200) len = 200;

        StoreLine *line = linestore_alloc_line(ls);
        if (!line) return;
        line->type = LINE_TOOL_OUTPUT;
        line->raw_text = strndup(p, len);
        line->indent = 4;
        line->wrap_count = compute_wrap_count(line->raw_text, ls->content_width - 4);
        ls->total_screen_rows += line->wrap_count;
        lines_added++;

        p += (eol ? (int)(eol - p) : len);
        if (eol) p++;
    }
    if (*p) {
        StoreLine *line = linestore_alloc_line(ls);
        if (!line) return;
        line->type = LINE_TOOL_OUTPUT;
        line->raw_text = strdup("...");
        line->indent = 4;
        line->wrap_count = 1;
        ls->total_screen_rows++;
    }
}

void linestore_add_tool_done(LineStore *ls, const char *name) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_TOOL_DONE;
    int nlen = name ? (int)strlen(name) : 0;
    line->raw_text = malloc(nlen + 16);
    if (!line->raw_text) { ls->count--; return; }
    snprintf(line->raw_text, nlen + 16, "\xe2\x9f\xa1 %s", name ? name : "");
    line->indent = 2;
    line->wrap_count = 1;
    ls->total_screen_rows++;
}

void linestore_add_splash(LineStore *ls, const char *text, float brightness) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_SPLASH;
    line->raw_text = strdup(text ? text : "");
    line->brightness = brightness;
    line->wrap_count = 1;
    ls->total_screen_rows++;
}

static float layer_brightness(char layer) {
    switch (layer) {
        case 'O': return 1.0f;    /* outer edge */
        case 'M': return 0.55f;   /* mid sail */
        case 'I': return 0.30f;   /* inner sail */
        case 'm': return 0.70f;   /* mast */
        case 'L': return 0.85f;   /* logo */
        case 'i': return 0.35f;   /* info text */
        case 'b': return 0.50f;   /* base line */
        default:  return 0.0f;    /* space */
    }
}

void linestore_add_splash_layered(LineStore *ls, const char *text, const char *layers) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_SPLASH;
    line->raw_text = strdup(text ? text : "");
    line->brightness = 1.0f;
    line->wrap_count = 1;

    /* Build spans from layer map — group consecutive same-layer chars */
    const unsigned char *tp = (const unsigned char *)line->raw_text;
    int li = 0;
    int layers_len = layers ? (int)strlen(layers) : 0;

    int span_cap = 16;
    Span *spans = calloc(span_cap, sizeof(Span));
    int span_count = 0;

    while (*tp && li < layers_len) {
        char layer = layers[li];
        float br = layer_brightness(layer);

        const char *start = (const char *)tp;
        int char_count = 0;

        /* Group consecutive chars with same layer */
        while (*tp && li < layers_len && layers[li] == layer) {
            int clen = 1;
            if (*tp >= 0xF0) clen = 4;
            else if (*tp >= 0xE0) clen = 3;
            else if (*tp >= 0xC0) clen = 2;
            tp += clen;
            li++;
            char_count++;
        }

        if (span_count >= span_cap) {
            span_cap *= 2;
            Span *ns = realloc(spans, (size_t)span_cap * sizeof(Span));
            if (!ns) break;
            spans = ns;
        }

        spans[span_count].text = start;
        spans[span_count].len = (int)((const char *)tp - start);
        spans[span_count].flags = SPAN_PLAIN;
        spans[span_count].brightness = br;
        span_count++;
    }

    line->spans = spans;
    line->span_count = span_count;
    ls->total_screen_rows++;
}

void linestore_add_error(LineStore *ls, const char *text) {
    StoreLine *line = linestore_alloc_line(ls);
    if (!line) return;
    line->type = LINE_ERROR;
    line->raw_text = strdup(text ? text : "error");
    line->wrap_count = compute_wrap_count(line->raw_text, ls->content_width);
    ls->total_screen_rows += line->wrap_count;
}

int linestore_screen_row_count(const LineStore *ls) {
    return ls->total_screen_rows;
}

ScreenRowRef linestore_row_to_line(const LineStore *ls, int screen_row) {
    int row = 0;
    for (int i = 0; i < ls->count; i++) {
        int next = row + ls->lines[i].wrap_count;
        if (screen_row < next) {
            return (ScreenRowRef){ .line_index = i, .wrap_offset = screen_row - row };
        }
        row = next;
    }
    return (ScreenRowRef){ .line_index = ls->count - 1, .wrap_offset = 0 };
}
