#include "editor.h"
#include "tui/keys.h"
#include "tui/ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char **lines;
    int line_count;
    int line_capacity;
    int cursor_line;
    int cursor_col;
    int scroll_offset;
    int visible_lines;
} EditorData;

static void ensure_line_capacity(EditorData *d, int needed) {
    while (needed >= d->line_capacity) {
        d->line_capacity *= 2;
        d->lines = realloc(d->lines, d->line_capacity * sizeof(char *));
    }
}

static char **editor_render(Component *self, int width, int *line_count) {
    EditorData *d = (EditorData *)self->data;
    int visible = d->visible_lines;
    if (visible > d->line_count) visible = d->line_count;
    if (visible <= 0) visible = 1;

    /* Adjust scroll offset to keep cursor visible */
    if (d->cursor_line < d->scroll_offset) {
        d->scroll_offset = d->cursor_line;
    }
    if (d->cursor_line >= d->scroll_offset + d->visible_lines) {
        d->scroll_offset = d->cursor_line - d->visible_lines + 1;
    }
    if (d->scroll_offset < 0) d->scroll_offset = 0;

    int end = d->scroll_offset + d->visible_lines;
    if (end > d->line_count) end = d->line_count;
    int out_count = end - d->scroll_offset;
    if (out_count <= 0) {
        /* show at least one empty line */
        char **out = calloc(1, sizeof(char *));
        /* gutter: "  1 │ " */
        int cap = 64;
        char *line = malloc(cap);
        snprintf(line, cap, "%s%3d \xE2\x94\x82%s ", "\x1b[2m", 1, "\x1b[0m");
        out[0] = line;
        *line_count = 1;
        return out;
    }

    /* Calculate gutter width based on total lines */
    int max_line_num = d->line_count;
    int gutter_digits = 1;
    {
        int tmp = max_line_num;
        while (tmp >= 10) { gutter_digits++; tmp /= 10; }
    }
    if (gutter_digits < 3) gutter_digits = 3;

    char **out = calloc(out_count, sizeof(char *));
    for (int i = 0; i < out_count; i++) {
        int line_idx = d->scroll_offset + i;
        const char *content = d->lines[line_idx];
        int clen = content ? (int)strlen(content) : 0;
        int is_current = (line_idx == d->cursor_line);

        /* Format: "  N │ content" with dim line numbers */
        /* current line gets highlight */
        int cap = clen + gutter_digits + 64;
        char *buf = malloc(cap);
        int pos = 0;

        /* line number (dim) */
        pos += snprintf(buf + pos, cap - pos, "\x1b[2m%*d \xE2\x94\x82\x1b[0m ",
                       gutter_digits, line_idx + 1);

        if (is_current && self->focused) {
            /* highlight current line */
            pos += snprintf(buf + pos, cap - pos, "\x1b[47;30m");
        }

        /* content - truncate to available width */
        int gutter_vis = gutter_digits + 3; /* digits + " │ " */
        int avail = width - gutter_vis;
        if (avail < 1) avail = 1;

        int copy_len = clen;
        if (copy_len > avail) copy_len = avail;
        if (copy_len > 0) {
            while (pos + copy_len >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            memcpy(buf + pos, content, copy_len);
            pos += copy_len;
        }

        /* pad to width for current line highlight */
        if (is_current && self->focused) {
            int pad = avail - copy_len;
            while (pos + pad + 8 >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            for (int j = 0; j < pad; j++) buf[pos++] = ' ';
            pos += snprintf(buf + pos, cap - pos, "\x1b[0m");
        }

        buf[pos] = '\0';
        out[i] = buf;
    }

    *line_count = out_count;
    return out;
}

static void editor_handle(Component *self, const char *data, int len) {
    EditorData *d = (EditorData *)self->data;
    ParsedKey key = key_parse(data, len);

    if (key_matches(&key, "enter")) {
        /* Split current line at cursor */
        const char *cur = d->lines[d->cursor_line];
        int cur_len = cur ? (int)strlen(cur) : 0;
        int col = d->cursor_col;
        if (col > cur_len) col = cur_len;

        char *before = strndup(cur, col);
        char *after = strdup(cur + col);

        ensure_line_capacity(d, d->line_count + 1);

        /* Shift lines down */
        for (int i = d->line_count; i > d->cursor_line + 1; i--) {
            d->lines[i] = d->lines[i - 1];
        }

        free(d->lines[d->cursor_line]);
        d->lines[d->cursor_line] = before;
        d->lines[d->cursor_line + 1] = after;
        d->line_count++;
        d->cursor_line++;
        d->cursor_col = 0;

    } else if (key_matches(&key, "backspace")) {
        if (d->cursor_col > 0) {
            char *cur = d->lines[d->cursor_line];
            int cur_len = (int)strlen(cur);
            int col = d->cursor_col;
            if (col > cur_len) col = cur_len;
            memmove(cur + col - 1, cur + col, cur_len - col + 1);
            d->cursor_col--;
        } else if (d->cursor_line > 0) {
            /* Join with previous line */
            int prev = d->cursor_line - 1;
            int prev_len = (int)strlen(d->lines[prev]);
            int cur_len = (int)strlen(d->lines[d->cursor_line]);
            char *merged = malloc(prev_len + cur_len + 1);
            memcpy(merged, d->lines[prev], prev_len);
            memcpy(merged + prev_len, d->lines[d->cursor_line], cur_len);
            merged[prev_len + cur_len] = '\0';

            free(d->lines[prev]);
            free(d->lines[d->cursor_line]);

            /* Shift lines up */
            for (int i = d->cursor_line; i < d->line_count - 1; i++) {
                d->lines[i] = d->lines[i + 1];
            }
            d->lines[d->line_count - 1] = NULL;
            d->line_count--;

            d->lines[prev] = merged;
            d->cursor_line = prev;
            d->cursor_col = prev_len;
        }

    } else if (key_matches(&key, "left") || key_matches(&key, "ctrl+b")) {
        if (d->cursor_col > 0) {
            d->cursor_col--;
        } else if (d->cursor_line > 0) {
            d->cursor_line--;
            d->cursor_col = (int)strlen(d->lines[d->cursor_line]);
        }

    } else if (key_matches(&key, "right") || key_matches(&key, "ctrl+f")) {
        int cur_len = (int)strlen(d->lines[d->cursor_line]);
        if (d->cursor_col < cur_len) {
            d->cursor_col++;
        } else if (d->cursor_line < d->line_count - 1) {
            d->cursor_line++;
            d->cursor_col = 0;
        }

    } else if (key_matches(&key, "up")) {
        if (d->cursor_line > 0) {
            d->cursor_line--;
            int line_len = (int)strlen(d->lines[d->cursor_line]);
            if (d->cursor_col > line_len) d->cursor_col = line_len;
        }

    } else if (key_matches(&key, "down")) {
        if (d->cursor_line < d->line_count - 1) {
            d->cursor_line++;
            int line_len = (int)strlen(d->lines[d->cursor_line]);
            if (d->cursor_col > line_len) d->cursor_col = line_len;
        }

    } else if (key_matches(&key, "home") || key_matches(&key, "ctrl+a")) {
        d->cursor_col = 0;

    } else if (key_matches(&key, "end") || key_matches(&key, "ctrl+e")) {
        d->cursor_col = (int)strlen(d->lines[d->cursor_line]);

    } else if (key_matches(&key, "ctrl+k")) {
        /* Kill to end of line */
        char *cur = d->lines[d->cursor_line];
        int col = d->cursor_col;
        if (col <= (int)strlen(cur)) {
            cur[col] = '\0';
        }

    } else if (key_matches(&key, "ctrl+u")) {
        /* Kill to start of line */
        char *cur = d->lines[d->cursor_line];
        int cur_len = (int)strlen(cur);
        int col = d->cursor_col;
        if (col > cur_len) col = cur_len;
        memmove(cur, cur + col, cur_len - col + 1);
        d->cursor_col = 0;

    } else if (key_matches(&key, "tab")) {
        /* Insert 2 spaces */
        char *cur = d->lines[d->cursor_line];
        int cur_len = (int)strlen(cur);
        char *new_line = malloc(cur_len + 3);
        int col = d->cursor_col;
        if (col > cur_len) col = cur_len;
        memcpy(new_line, cur, col);
        new_line[col] = ' ';
        new_line[col + 1] = ' ';
        memcpy(new_line + col + 2, cur + col, cur_len - col + 1);
        free(d->lines[d->cursor_line]);
        d->lines[d->cursor_line] = new_line;
        d->cursor_col += 2;

    } else if (key.printable[0]) {
        int insert_len = (int)strlen(key.printable);
        char *cur = d->lines[d->cursor_line];
        int cur_len = (int)strlen(cur);
        int col = d->cursor_col;
        if (col > cur_len) col = cur_len;
        char *new_line = malloc(cur_len + insert_len + 1);
        memcpy(new_line, cur, col);
        memcpy(new_line + col, key.printable, insert_len);
        memcpy(new_line + col + insert_len, cur + col, cur_len - col + 1);
        free(d->lines[d->cursor_line]);
        d->lines[d->cursor_line] = new_line;
        d->cursor_col += insert_len;
    }
}

static void editor_free(Component *self) {
    EditorData *d = (EditorData *)self->data;
    for (int i = 0; i < d->line_count; i++) {
        free(d->lines[i]);
    }
    free(d->lines);
    free(d);
}

Component *widget_editor_create(int visible_lines) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    EditorData *d = calloc(1, sizeof(EditorData));
    if (!d) { free(comp); return NULL; }

    d->visible_lines = visible_lines > 0 ? visible_lines : 10;
    d->line_capacity = 16;
    d->lines = calloc(d->line_capacity, sizeof(char *));
    d->lines[0] = strdup("");
    d->line_count = 1;

    comp->data = d;
    comp->render = editor_render;
    comp->handle_input = editor_handle;
    comp->free_comp = editor_free;
    comp->focused = true;

    return comp;
}

const char *widget_editor_get_text(Component *comp) {
    if (!comp) return NULL;
    EditorData *d = (EditorData *)comp->data;

    /* Build full text by joining lines with \n */
    /* We store it as a static-like buffer in the first line's extra space */
    /* Actually, return a freshly built string in a reusable buffer */
    /* For simplicity, use a static thread-local buffer approach */
    static char *result_buf = NULL;
    free(result_buf);

    int total = 0;
    for (int i = 0; i < d->line_count; i++) {
        total += (int)strlen(d->lines[i]);
        if (i < d->line_count - 1) total++; /* \n */
    }

    result_buf = malloc(total + 1);
    int pos = 0;
    for (int i = 0; i < d->line_count; i++) {
        int len = (int)strlen(d->lines[i]);
        memcpy(result_buf + pos, d->lines[i], len);
        pos += len;
        if (i < d->line_count - 1) {
            result_buf[pos++] = '\n';
        }
    }
    result_buf[pos] = '\0';
    return result_buf;
}

void widget_editor_set_text(Component *comp, const char *text) {
    if (!comp) return;
    EditorData *d = (EditorData *)comp->data;

    /* Free old lines */
    for (int i = 0; i < d->line_count; i++) {
        free(d->lines[i]);
    }
    d->line_count = 0;

    if (!text || !*text) {
        d->lines[0] = strdup("");
        d->line_count = 1;
        d->cursor_line = 0;
        d->cursor_col = 0;
        d->scroll_offset = 0;
        return;
    }

    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        ensure_line_capacity(d, d->line_count + 1);
        d->lines[d->line_count++] = strndup(p, len);
        p += len;
        if (nl) p++;
    }

    /* Handle trailing newline */
    if (text[strlen(text) - 1] == '\n') {
        ensure_line_capacity(d, d->line_count + 1);
        d->lines[d->line_count++] = strdup("");
    }

    if (d->line_count == 0) {
        d->lines[0] = strdup("");
        d->line_count = 1;
    }

    d->cursor_line = 0;
    d->cursor_col = 0;
    d->scroll_offset = 0;
}

void widget_editor_clear(Component *comp) {
    widget_editor_set_text(comp, "");
}

int widget_editor_get_cursor_line(Component *comp) {
    if (!comp) return 0;
    EditorData *d = (EditorData *)comp->data;
    return d->cursor_line;
}

int widget_editor_get_cursor_col(Component *comp) {
    if (!comp) return 0;
    EditorData *d = (EditorData *)comp->data;
    return d->cursor_col;
}

int widget_editor_get_line_count(Component *comp) {
    if (!comp) return 0;
    EditorData *d = (EditorData *)comp->data;
    return d->line_count;
}
