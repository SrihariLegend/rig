#include "select_list.h"
#include "tui/keys.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    SelectItem *items;
    int item_count;
    int selected;
    int scroll_offset;
} SelectListData;

static char **select_render(Component *self, int width, int *line_count) {
    SelectListData *d = (SelectListData *)self->data;
    if (d->item_count == 0) {
        *line_count = 0;
        return NULL;
    }

    int visible = d->item_count < 10 ? d->item_count : 10;
    char **lines = calloc(visible, sizeof(char *));

    for (int i = 0; i < visible; i++) {
        int idx = d->scroll_offset + i;
        if (idx >= d->item_count) {
            lines[i] = strdup("");
            continue;
        }

        const char *prefix = (idx == d->selected) ? "\x1b[7m" : "  ";
        const char *suffix = (idx == d->selected) ? "\x1b[0m" : "";
        const char *label = d->items[idx].label ? d->items[idx].label : "";
        const char *desc = d->items[idx].description ? d->items[idx].description : "";

        char *line = malloc(strlen(label) + strlen(desc) + 32);
        if (desc[0]) {
            snprintf(line, strlen(label) + strlen(desc) + 32,
                     "%s%-*s %s%s", prefix, width / 2, label, desc, suffix);
        } else {
            snprintf(line, strlen(label) + 32, "%s%s%s", prefix, label, suffix);
        }
        lines[i] = line;
    }

    *line_count = visible;
    return lines;
}

static void select_handle_input(Component *self, const char *data, int len) {
    SelectListData *d = (SelectListData *)self->data;
    ParsedKey key = key_parse(data, len);

    if (key_matches(&key, "up") || key_matches(&key, "ctrl+p")) {
        if (d->selected > 0) {
            d->selected--;
            if (d->selected < d->scroll_offset) d->scroll_offset = d->selected;
        }
    } else if (key_matches(&key, "down") || key_matches(&key, "ctrl+n")) {
        if (d->selected < d->item_count - 1) {
            d->selected++;
            if (d->selected >= d->scroll_offset + 10) d->scroll_offset = d->selected - 9;
        }
    }
}

static void select_free(Component *self) {
    SelectListData *d = (SelectListData *)self->data;
    for (int i = 0; i < d->item_count; i++) {
        free(d->items[i].label);
        free(d->items[i].value);
        free(d->items[i].description);
    }
    free(d->items);
    free(d);
}

Component *widget_select_list_create(SelectItem *items, int item_count) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    SelectListData *d = calloc(1, sizeof(SelectListData));
    if (!d) { free(comp); return NULL; }

    d->items = calloc(item_count, sizeof(SelectItem));
    d->item_count = item_count;
    for (int i = 0; i < item_count; i++) {
        d->items[i].label = items[i].label ? strdup(items[i].label) : NULL;
        d->items[i].value = items[i].value ? strdup(items[i].value) : NULL;
        d->items[i].description = items[i].description ? strdup(items[i].description) : NULL;
    }

    comp->data = d;
    comp->render = select_render;
    comp->handle_input = select_handle_input;
    comp->free_comp = select_free;
    comp->focused = true;

    return comp;
}

int widget_select_list_get_selected(Component *comp) {
    if (!comp) return -1;
    SelectListData *d = (SelectListData *)comp->data;
    return d->selected;
}

const char *widget_select_list_get_value(Component *comp) {
    if (!comp) return NULL;
    SelectListData *d = (SelectListData *)comp->data;
    if (d->selected < 0 || d->selected >= d->item_count) return NULL;
    return d->items[d->selected].value;
}
