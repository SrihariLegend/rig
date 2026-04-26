#include "markdown.h"
#include "tui/ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_DIM     "\x1b[2m"
#define ANSI_ITALIC  "\x1b[3m"
#define ANSI_UNDERLINE "\x1b[4m"
#define ANSI_INVERSE "\x1b[7m"
#define ANSI_BOLD_CYAN "\x1b[1;36m"
#define ANSI_CODE_BG "\x1b[48;5;236m"

typedef enum {
    MD_NORMAL,
    MD_CODE_BLOCK,
} MdBlockState;

typedef struct {
    char *text;
    char **cached_lines;
    int cached_count;
    int cached_width;
} MarkdownData;

static void free_cached(MarkdownData *d) {
    if (d->cached_lines) {
        for (int i = 0; i < d->cached_count; i++) free(d->cached_lines[i]);
        free(d->cached_lines);
        d->cached_lines = NULL;
        d->cached_count = 0;
    }
}

/* --- Line output buffer --- */

typedef struct {
    char **lines;
    int count;
    int capacity;
} LineBuffer;

static void lb_init(LineBuffer *lb) {
    lb->capacity = 32;
    lb->lines = calloc(lb->capacity, sizeof(char *));
    lb->count = 0;
}

static void lb_push(LineBuffer *lb, char *line) {
    if (lb->count >= lb->capacity) {
        lb->capacity *= 2;
        lb->lines = realloc(lb->lines, lb->capacity * sizeof(char *));
    }
    lb->lines[lb->count++] = line;
}

/* --- Inline formatting --- */

static void append_str(char **buf, int *len, int *cap, const char *s, int slen) {
    while (*len + slen >= *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void append_cstr(char **buf, int *len, int *cap, const char *s) {
    append_str(buf, len, cap, s, (int)strlen(s));
}

static char *format_inline(const char *src, int src_len) {
    int cap = src_len * 3 + 64;
    int len = 0;
    char *out = malloc(cap);
    out[0] = '\0';

    int i = 0;
    int bold_open = 0;
    int italic_open = 0;

    while (i < src_len) {
        /* inline code */
        if (src[i] == '`') {
            int end = i + 1;
            while (end < src_len && src[end] != '`') end++;
            if (end < src_len) {
                append_cstr(&out, &len, &cap, ANSI_INVERSE);
                append_str(&out, &len, &cap, src + i + 1, end - i - 1);
                append_cstr(&out, &len, &cap, ANSI_RESET);
                i = end + 1;
                continue;
            }
        }

        /* bold: ** */
        if (i + 1 < src_len && src[i] == '*' && src[i + 1] == '*') {
            if (!bold_open) {
                append_cstr(&out, &len, &cap, ANSI_BOLD);
                bold_open = 1;
            } else {
                append_cstr(&out, &len, &cap, ANSI_RESET);
                bold_open = 0;
            }
            i += 2;
            continue;
        }

        /* italic: single * (not **) */
        if (src[i] == '*' && !(i + 1 < src_len && src[i + 1] == '*')) {
            if (!italic_open) {
                append_cstr(&out, &len, &cap, ANSI_ITALIC);
                italic_open = 1;
            } else {
                append_cstr(&out, &len, &cap, ANSI_RESET);
                italic_open = 0;
            }
            i++;
            continue;
        }

        /* link: [text](url) */
        if (src[i] == '[') {
            int text_end = i + 1;
            while (text_end < src_len && src[text_end] != ']') text_end++;
            if (text_end < src_len - 1 && src[text_end + 1] == '(') {
                int url_end = text_end + 2;
                while (url_end < src_len && src[url_end] != ')') url_end++;
                if (url_end < src_len) {
                    append_cstr(&out, &len, &cap, ANSI_UNDERLINE);
                    append_str(&out, &len, &cap, src + i + 1, text_end - i - 1);
                    append_cstr(&out, &len, &cap, ANSI_RESET);
                    append_cstr(&out, &len, &cap, " ");
                    append_cstr(&out, &len, &cap, ANSI_DIM);
                    append_str(&out, &len, &cap, src + text_end + 2, url_end - text_end - 2);
                    append_cstr(&out, &len, &cap, ANSI_RESET);
                    i = url_end + 1;
                    continue;
                }
            }
        }

        append_str(&out, &len, &cap, src + i, 1);
        i++;
    }

    /* close any unclosed formatting */
    if (bold_open || italic_open) {
        append_cstr(&out, &len, &cap, ANSI_RESET);
    }

    return out;
}

/* --- Word wrap that respects ANSI --- */

static void wrap_line(const char *formatted, int width, LineBuffer *lb,
                      const char *prefix) {
    if (width <= 0) width = 80;
    int prefix_vis = prefix ? unicode_display_width(prefix) : 0;
    int avail = width - prefix_vis;
    if (avail < 4) avail = 4;

    char *stripped = ansi_strip(formatted);
    int vis_len = stripped ? unicode_display_width(stripped) : 0;
    free(stripped);

    if (vis_len <= avail) {
        int plen = prefix ? (int)strlen(prefix) : 0;
        int flen = (int)strlen(formatted);
        char *line = malloc(plen + flen + 1);
        if (prefix) memcpy(line, prefix, plen);
        memcpy(line + plen, formatted, flen);
        line[plen + flen] = '\0';
        lb_push(lb, line);
        return;
    }

    /* Simple approach: walk formatted string, track visible width */
    const char *p = formatted;
    int flen = (int)strlen(formatted);

    while (*p) {
        int cap = 256;
        int len = 0;
        char *chunk = malloc(cap);
        chunk[0] = '\0';

        if (prefix) {
            append_cstr(&chunk, &len, &cap, prefix);
        }

        int vis_w = 0;
        const char *seg = p;
        int last_space_offset = -1;
        int last_space_vis = 0;

        while (*seg && vis_w < avail) {
            if (*seg == '\x1b') {
                /* skip ANSI sequence */
                const char *end = seg + 1;
                if (*end == '[') {
                    end++;
                    while (*end && !isalpha((unsigned char)*end)) end++;
                    if (*end) end++;
                } else if (*end == ']') {
                    end++;
                    while (*end && *end != '\x07' && *end != '\x1b') end++;
                    if (*end == '\x07') end++;
                    else if (*end == '\x1b' && *(end+1) == '\\') end += 2;
                }
                int esc_len = (int)(end - seg);
                append_str(&chunk, &len, &cap, seg, esc_len);
                seg = end;
                continue;
            }

            if (*seg == ' ') {
                last_space_offset = (int)(seg - p);
                last_space_vis = vis_w;
            }

            /* decode one UTF-8 char */
            unsigned char c = (unsigned char)*seg;
            int char_bytes = 1;
            if (c >= 0xF0) char_bytes = 4;
            else if (c >= 0xE0) char_bytes = 3;
            else if (c >= 0xC0) char_bytes = 2;

            /* check remaining */
            int remaining = flen - (int)(seg - formatted);
            if (char_bytes > remaining) char_bytes = remaining;

            unsigned int cp = 0;
            if (char_bytes == 1) cp = c;
            else if (char_bytes == 2) cp = ((c & 0x1F) << 6) | (seg[1] & 0x3F);
            else if (char_bytes == 3) cp = ((c & 0x0F) << 12) | ((seg[1] & 0x3F) << 6) | (seg[2] & 0x3F);
            else if (char_bytes == 4) cp = ((c & 0x07) << 18) | ((seg[1] & 0x3F) << 12) | ((seg[2] & 0x3F) << 6) | (seg[3] & 0x3F);

            int cw = unicode_char_width(cp);
            if (vis_w + cw > avail && vis_w > 0) break;

            append_str(&chunk, &len, &cap, seg, char_bytes);
            vis_w += cw;
            seg += char_bytes;
        }

        if (*seg && last_space_offset >= 0 && vis_w >= avail) {
            /* rewind to last space */
            int chars_to_remove = (int)(seg - (p + last_space_offset + 1));
            (void)chars_to_remove;
            /* simpler: just truncate at last space */
            /* rebuild chunk from prefix + content up to last_space_offset */
            free(chunk);
            int plen2 = prefix ? (int)strlen(prefix) : 0;
            cap = plen2 + last_space_offset + 64;
            len = 0;
            chunk = malloc(cap);
            chunk[0] = '\0';
            if (prefix) append_cstr(&chunk, &len, &cap, prefix);

            /* copy raw bytes from p up to visible space position */
            const char *q = p;
            int vis2 = 0;
            while (q < p + flen && vis2 <= last_space_vis) {
                if (*q == '\x1b') {
                    const char *end = q + 1;
                    if (*end == '[') {
                        end++;
                        while (*end && !isalpha((unsigned char)*end)) end++;
                        if (*end) end++;
                    }
                    int esc_len = (int)(end - q);
                    append_str(&chunk, &len, &cap, q, esc_len);
                    q = end;
                    continue;
                }
                if (vis2 == last_space_vis && *q == ' ') {
                    q++;
                    break;
                }
                unsigned char uc = (unsigned char)*q;
                int cb = 1;
                if (uc >= 0xF0) cb = 4;
                else if (uc >= 0xE0) cb = 3;
                else if (uc >= 0xC0) cb = 2;
                if (*q == ' ') vis2++;
                else {
                    unsigned int cpp = 0;
                    if (cb == 1) cpp = uc;
                    else if (cb == 2) cpp = ((uc & 0x1F) << 6) | (q[1] & 0x3F);
                    else if (cb == 3) cpp = ((uc & 0x0F) << 12) | ((q[1] & 0x3F) << 6) | (q[2] & 0x3F);
                    else if (cb == 4) cpp = ((uc & 0x07) << 18) | ((q[1] & 0x3F) << 12) | ((q[2] & 0x3F) << 6) | (q[3] & 0x3F);
                    vis2 += unicode_char_width(cpp);
                }
                append_str(&chunk, &len, &cap, q, cb);
                q += cb;
            }
            seg = q;
        }

        lb_push(lb, chunk);
        p = seg;
        /* skip leading spaces on next line */
        while (*p == ' ') p++;
        if (!*p) break;
    }
}

/* --- Main renderer --- */

static void render_markdown(const char *text, int width, LineBuffer *lb) {
    if (!text || !*text) return;

    MdBlockState state = MD_NORMAL;
    const char *p = text;

    while (*p) {
        const char *line_end = strchr(p, '\n');
        int line_len = line_end ? (int)(line_end - p) : (int)strlen(p);
        char *line = strndup(p, line_len);

        if (state == MD_CODE_BLOCK) {
            if (strncmp(line, "```", 3) == 0) {
                state = MD_NORMAL;
            } else {
                /* code block line: 2-space indent, background color */
                int cap = line_len + 64;
                char *out = malloc(cap);
                snprintf(out, cap, "  %s%s%s", ANSI_CODE_BG, line, ANSI_RESET);
                lb_push(lb, out);
            }
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* code block start */
        if (strncmp(line, "```", 3) == 0) {
            state = MD_CODE_BLOCK;
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* horizontal rule */
        if (strcmp(line, "---") == 0 || strcmp(line, "***") == 0 || strcmp(line, "___") == 0) {
            int w = width > 0 ? width : 80;
            /* Each ─ is 3 bytes in UTF-8 */
            int rule_bytes = w * 3;
            char *rule = malloc(rule_bytes + 1);
            int pos = 0;
            for (int i = 0; i < w; i++) {
                rule[pos++] = (char)0xE2;
                rule[pos++] = (char)0x94;
                rule[pos++] = (char)0x80;
            }
            rule[pos] = '\0';
            lb_push(lb, rule);
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* blank line = paragraph break */
        if (line_len == 0) {
            lb_push(lb, strdup(""));
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* heading: # */
        if (line[0] == '#') {
            int level = 0;
            while (level < line_len && line[level] == '#') level++;
            const char *heading_text = line + level;
            while (*heading_text == ' ') heading_text++;
            char *formatted = format_inline(heading_text, (int)strlen(heading_text));
            int flen = (int)strlen(formatted);
            int cap = flen + 32;
            char *out = malloc(cap);
            if (level == 1) {
                snprintf(out, cap, "%s%s%s", ANSI_BOLD_CYAN, formatted, ANSI_RESET);
            } else {
                snprintf(out, cap, "%s%s%s", ANSI_BOLD, formatted, ANSI_RESET);
            }
            free(formatted);
            lb_push(lb, out);
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* blockquote: > */
        if (line[0] == '>') {
            const char *quote_text = line + 1;
            while (*quote_text == ' ') quote_text++;
            char *formatted = format_inline(quote_text, (int)strlen(quote_text));
            /* prefix with dim bar */
            char prefix[32];
            /* │ is E2 94 82 in UTF-8 */
            snprintf(prefix, sizeof(prefix), "%s\xE2\x94\x82 ", ANSI_DIM);
            char *reset_suffix = ANSI_RESET;
            int flen = (int)strlen(formatted);
            int plen = (int)strlen(prefix);
            int rlen = (int)strlen(reset_suffix);
            char *out = malloc(plen + flen + rlen + 1);
            memcpy(out, prefix, plen);
            memcpy(out + plen, formatted, flen);
            memcpy(out + plen + flen, reset_suffix, rlen);
            out[plen + flen + rlen] = '\0';
            free(formatted);
            lb_push(lb, out);
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* unordered list: - item or * item */
        if ((line[0] == '-' || line[0] == '*') && line_len > 1 && line[1] == ' ') {
            const char *item_text = line + 2;
            char *formatted = format_inline(item_text, (int)strlen(item_text));
            /* bullet: "  • " (• is E2 80 A2) */
            wrap_line(formatted, width, lb, "  \xE2\x80\xA2 ");
            free(formatted);
            free(line);
            p += line_len;
            if (line_end) p++;
            continue;
        }

        /* numbered list: 1. item */
        if (isdigit((unsigned char)line[0])) {
            int ni = 0;
            while (ni < line_len && isdigit((unsigned char)line[ni])) ni++;
            if (ni < line_len && line[ni] == '.' && ni + 1 < line_len && line[ni + 1] == ' ') {
                char num_prefix[16];
                snprintf(num_prefix, sizeof(num_prefix), "  %.*s. ", ni, line);
                const char *item_text = line + ni + 2;
                char *formatted = format_inline(item_text, (int)strlen(item_text));
                wrap_line(formatted, width, lb, num_prefix);
                free(formatted);
                free(line);
                p += line_len;
                if (line_end) p++;
                continue;
            }
        }

        /* normal paragraph */
        {
            char *formatted = format_inline(line, line_len);
            wrap_line(formatted, width, lb, NULL);
            free(formatted);
        }

        free(line);
        p += line_len;
        if (line_end) p++;
    }
}

static char **markdown_render(Component *self, int width, int *line_count) {
    MarkdownData *d = (MarkdownData *)self->data;
    if (!d->text) {
        *line_count = 0;
        return NULL;
    }

    if (d->cached_lines && d->cached_width == width) {
        char **copy = calloc(d->cached_count, sizeof(char *));
        for (int i = 0; i < d->cached_count; i++) {
            copy[i] = strdup(d->cached_lines[i]);
        }
        *line_count = d->cached_count;
        return copy;
    }

    free_cached(d);

    LineBuffer lb;
    lb_init(&lb);
    render_markdown(d->text, width, &lb);

    d->cached_lines = lb.lines;
    d->cached_count = lb.count;
    d->cached_width = width;

    if (lb.count == 0) {
        *line_count = 0;
        return NULL;
    }

    char **copy = calloc(lb.count, sizeof(char *));
    for (int i = 0; i < lb.count; i++) {
        copy[i] = strdup(lb.lines[i]);
    }
    *line_count = lb.count;
    return copy;
}

static void markdown_invalidate(Component *self) {
    MarkdownData *d = (MarkdownData *)self->data;
    free_cached(d);
}

static void markdown_free(Component *self) {
    MarkdownData *d = (MarkdownData *)self->data;
    free_cached(d);
    free(d->text);
    free(d);
}

Component *widget_markdown_create(const char *markdown_text) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    MarkdownData *d = calloc(1, sizeof(MarkdownData));
    if (!d) { free(comp); return NULL; }

    d->text = markdown_text ? strdup(markdown_text) : NULL;

    comp->data = d;
    comp->render = markdown_render;
    comp->invalidate = markdown_invalidate;
    comp->free_comp = markdown_free;

    return comp;
}

void widget_markdown_set(Component *comp, const char *text) {
    if (!comp) return;
    MarkdownData *d = (MarkdownData *)comp->data;
    free(d->text);
    d->text = text ? strdup(text) : NULL;
    free_cached(d);
}

void widget_markdown_append(Component *comp, const char *text) {
    if (!comp || !text) return;
    MarkdownData *d = (MarkdownData *)comp->data;
    if (!d->text) {
        d->text = strdup(text);
    } else {
        int old_len = (int)strlen(d->text);
        int add_len = (int)strlen(text);
        char *new_text = malloc(old_len + add_len + 1);
        memcpy(new_text, d->text, old_len);
        memcpy(new_text + old_len, text, add_len);
        new_text[old_len + add_len] = '\0';
        free(d->text);
        d->text = new_text;
    }
    free_cached(d);
}
