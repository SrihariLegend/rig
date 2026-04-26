#include "text.h"
#include "tui/ansi.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    char **cached_lines;
    int cached_count;
    int cached_width;
} TextData;

static void free_cached(TextData *d) {
    if (d->cached_lines) {
        for (int i = 0; i < d->cached_count; i++) free(d->cached_lines[i]);
        free(d->cached_lines);
        d->cached_lines = NULL;
        d->cached_count = 0;
    }
}

static char **word_wrap(const char *text, int width, int *line_count) {
    if (!text || width <= 0) {
        *line_count = 0;
        return NULL;
    }

    int capacity = 16;
    char **lines = calloc(capacity, sizeof(char *));
    int count = 0;

    const char *p = text;
    while (*p) {
        const char *line_end = strchr(p, '\n');
        int line_len = line_end ? (int)(line_end - p) : (int)strlen(p);

        if (line_len <= width) {
            if (count >= capacity) {
                capacity *= 2;
                lines = realloc(lines, capacity * sizeof(char *));
            }
            lines[count++] = strndup(p, line_len);
        } else {
            int pos = 0;
            while (pos < line_len) {
                int chunk = (line_len - pos > width) ? width : line_len - pos;

                int break_at = chunk;
                if (pos + chunk < line_len) {
                    for (int i = chunk; i > 0; i--) {
                        if (p[pos + i] == ' ') {
                            break_at = i;
                            break;
                        }
                    }
                }

                if (count >= capacity) {
                    capacity *= 2;
                    lines = realloc(lines, capacity * sizeof(char *));
                }
                lines[count++] = strndup(p + pos, break_at);
                pos += break_at;
                while (pos < line_len && p[pos] == ' ') pos++;
            }
        }

        p += line_len;
        if (line_end) p++;
    }

    *line_count = count;
    return lines;
}

static char **text_render(Component *self, int width, int *line_count) {
    TextData *d = (TextData *)self->data;
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
    d->cached_lines = word_wrap(d->text, width, &d->cached_count);
    d->cached_width = width;

    char **copy = calloc(d->cached_count, sizeof(char *));
    for (int i = 0; i < d->cached_count; i++) {
        copy[i] = strdup(d->cached_lines[i]);
    }
    *line_count = d->cached_count;
    return copy;
}

static void text_invalidate(Component *self) {
    TextData *d = (TextData *)self->data;
    free_cached(d);
}

static void text_free(Component *self) {
    TextData *d = (TextData *)self->data;
    free_cached(d);
    free(d->text);
    free(d);
}

Component *widget_text_create(const char *text) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    TextData *d = calloc(1, sizeof(TextData));
    if (!d) { free(comp); return NULL; }

    d->text = text ? strdup(text) : NULL;

    comp->data = d;
    comp->render = text_render;
    comp->invalidate = text_invalidate;
    comp->free_comp = text_free;

    return comp;
}

void widget_text_set(Component *comp, const char *text) {
    if (!comp) return;
    TextData *d = (TextData *)comp->data;
    free(d->text);
    d->text = text ? strdup(text) : NULL;
    free_cached(d);
}
