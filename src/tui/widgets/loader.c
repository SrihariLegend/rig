#include "loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *SPINNER_FRAMES[] = {
    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
    "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
    "\xe2\xa0\x87", "\xe2\xa0\x8f",
};
#define SPINNER_FRAME_COUNT 10

typedef struct {
    char *message;
    int frame;
} LoaderData;

static char **loader_render(Component *self, int width, int *line_count) {
    (void)width;
    LoaderData *d = (LoaderData *)self->data;
    char **lines = calloc(1, sizeof(char *));

    int msg_len = d->message ? (int)strlen(d->message) : 0;
    char *line = malloc(msg_len + 16);
    snprintf(line, msg_len + 16, "%s %s",
             SPINNER_FRAMES[d->frame % SPINNER_FRAME_COUNT],
             d->message ? d->message : "");
    lines[0] = line;

    *line_count = 1;
    return lines;
}

static void loader_free(Component *self) {
    LoaderData *d = (LoaderData *)self->data;
    free(d->message);
    free(d);
}

Component *widget_loader_create(const char *message) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    LoaderData *d = calloc(1, sizeof(LoaderData));
    if (!d) { free(comp); return NULL; }

    d->message = message ? strdup(message) : NULL;

    comp->data = d;
    comp->render = loader_render;
    comp->free_comp = loader_free;

    return comp;
}

void widget_loader_tick(Component *comp) {
    if (!comp) return;
    LoaderData *d = (LoaderData *)comp->data;
    d->frame = (d->frame + 1) % SPINNER_FRAME_COUNT;
}
