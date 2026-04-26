#include "input.h"
#include "tui/keys.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char *buffer;
    int buf_len;
    int buf_capacity;
    int cursor;
    char *placeholder;
    int scroll_offset;
} InputData;

static char **input_render(Component *self, int width, int *line_count) {
    InputData *d = (InputData *)self->data;
    char **lines = calloc(1, sizeof(char *));

    if (d->buf_len == 0 && d->placeholder) {
        int plen = (int)strlen(d->placeholder);
        char *line = malloc(plen + 20);
        snprintf(line, plen + 20, "\x1b[2m%s\x1b[0m", d->placeholder);
        lines[0] = line;
    } else {
        int visible_start = d->scroll_offset;
        int visible_len = d->buf_len - visible_start;
        if (visible_len > width - 2) visible_len = width - 2;
        if (visible_len < 0) visible_len = 0;

        int cursor_pos = d->cursor - visible_start;
        if (cursor_pos < 0) cursor_pos = 0;
        if (cursor_pos > visible_len) cursor_pos = visible_len;

        char *line = malloc(d->buf_len + 32);
        int pos = 0;

        if (visible_start < d->buf_len) {
            memcpy(line, d->buffer + visible_start, visible_len);
            pos = visible_len;
        }
        line[pos] = '\0';
        lines[0] = line;
    }

    *line_count = 1;
    return lines;
}

static void input_handle(Component *self, const char *data, int len) {
    InputData *d = (InputData *)self->data;
    ParsedKey key = key_parse(data, len);

    if (key_matches(&key, "backspace")) {
        if (d->cursor > 0) {
            memmove(d->buffer + d->cursor - 1, d->buffer + d->cursor, d->buf_len - d->cursor);
            d->cursor--;
            d->buf_len--;
            d->buffer[d->buf_len] = '\0';
        }
    } else if (key_matches(&key, "delete") || key_matches(&key, "ctrl+d")) {
        if (d->cursor < d->buf_len) {
            memmove(d->buffer + d->cursor, d->buffer + d->cursor + 1, d->buf_len - d->cursor - 1);
            d->buf_len--;
            d->buffer[d->buf_len] = '\0';
        }
    } else if (key_matches(&key, "left") || key_matches(&key, "ctrl+b")) {
        if (d->cursor > 0) d->cursor--;
    } else if (key_matches(&key, "right") || key_matches(&key, "ctrl+f")) {
        if (d->cursor < d->buf_len) d->cursor++;
    } else if (key_matches(&key, "home") || key_matches(&key, "ctrl+a")) {
        d->cursor = 0;
    } else if (key_matches(&key, "end") || key_matches(&key, "ctrl+e")) {
        d->cursor = d->buf_len;
    } else if (key_matches(&key, "ctrl+k")) {
        d->buf_len = d->cursor;
        d->buffer[d->buf_len] = '\0';
    } else if (key_matches(&key, "ctrl+u")) {
        memmove(d->buffer, d->buffer + d->cursor, d->buf_len - d->cursor);
        d->buf_len -= d->cursor;
        d->cursor = 0;
        d->buffer[d->buf_len] = '\0';
    } else if (key.printable[0]) {
        int insert_len = (int)strlen(key.printable);
        while (d->buf_len + insert_len >= d->buf_capacity) {
            d->buf_capacity *= 2;
            d->buffer = realloc(d->buffer, d->buf_capacity);
        }
        memmove(d->buffer + d->cursor + insert_len,
                d->buffer + d->cursor,
                d->buf_len - d->cursor);
        memcpy(d->buffer + d->cursor, key.printable, insert_len);
        d->cursor += insert_len;
        d->buf_len += insert_len;
        d->buffer[d->buf_len] = '\0';
    }
}

static void input_free(Component *self) {
    InputData *d = (InputData *)self->data;
    free(d->buffer);
    free(d->placeholder);
    free(d);
}

Component *widget_input_create(const char *placeholder) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    InputData *d = calloc(1, sizeof(InputData));
    if (!d) { free(comp); return NULL; }

    d->buf_capacity = 256;
    d->buffer = calloc(d->buf_capacity, 1);
    d->placeholder = placeholder ? strdup(placeholder) : NULL;

    comp->data = d;
    comp->render = input_render;
    comp->handle_input = input_handle;
    comp->free_comp = input_free;
    comp->focused = true;

    return comp;
}

const char *widget_input_get_text(Component *comp) {
    if (!comp) return NULL;
    InputData *d = (InputData *)comp->data;
    return d->buffer;
}

void widget_input_set_text(Component *comp, const char *text) {
    if (!comp) return;
    InputData *d = (InputData *)comp->data;

    int len = text ? (int)strlen(text) : 0;
    while (len >= d->buf_capacity) {
        d->buf_capacity *= 2;
        d->buffer = realloc(d->buffer, d->buf_capacity);
    }

    if (text) {
        memcpy(d->buffer, text, len);
    }
    d->buffer[len] = '\0';
    d->buf_len = len;
    d->cursor = len;
}

void widget_input_clear(Component *comp) {
    widget_input_set_text(comp, "");
}
