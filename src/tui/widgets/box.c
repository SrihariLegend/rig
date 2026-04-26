#include "box.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    Component *child;
    int padding;
} BoxData;

static char **box_render(Component *self, int width, int *line_count) {
    BoxData *d = (BoxData *)self->data;
    if (!d->child || !d->child->render) {
        *line_count = 0;
        return NULL;
    }

    int inner_width = width - d->padding * 2;
    if (inner_width < 1) inner_width = 1;

    int child_lines = 0;
    char **child_output = d->child->render(d->child, inner_width, &child_lines);

    int total = child_lines + d->padding * 2;
    char **lines = calloc(total, sizeof(char *));
    int idx = 0;

    for (int i = 0; i < d->padding; i++) {
        lines[idx++] = strdup("");
    }

    char pad_str[64] = "";
    for (int i = 0; i < d->padding && i < 62; i++) pad_str[i] = ' ';

    for (int i = 0; i < child_lines; i++) {
        int len = (child_output && child_output[i]) ? (int)strlen(child_output[i]) : 0;
        char *line = malloc(len + d->padding * 2 + 1);
        int pos = 0;
        for (int p = 0; p < d->padding; p++) line[pos++] = ' ';
        if (child_output && child_output[i]) {
            memcpy(line + pos, child_output[i], len);
            pos += len;
        }
        line[pos] = '\0';
        lines[idx++] = line;
    }

    for (int i = 0; i < d->padding; i++) {
        lines[idx++] = strdup("");
    }

    if (child_output) {
        for (int i = 0; i < child_lines; i++) free(child_output[i]);
        free(child_output);
    }

    *line_count = total;
    return lines;
}

static void box_handle_input(Component *self, const char *data, int len) {
    BoxData *d = (BoxData *)self->data;
    if (d->child && d->child->handle_input) {
        d->child->handle_input(d->child, data, len);
    }
}

static void box_free(Component *self) {
    BoxData *d = (BoxData *)self->data;
    free(d);
}

Component *widget_box_create(Component *child, int padding) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    BoxData *d = calloc(1, sizeof(BoxData));
    if (!d) { free(comp); return NULL; }

    d->child = child;
    d->padding = padding > 0 ? padding : 0;

    comp->data = d;
    comp->render = box_render;
    comp->handle_input = box_handle_input;
    comp->free_comp = box_free;

    return comp;
}
