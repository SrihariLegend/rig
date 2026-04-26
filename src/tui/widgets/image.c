#include "image.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char *base64_data;
    char *mime_type;
    int width_cells;
    int height_cells;
    ImageProtocol protocol;
} ImageData;

static char **image_render(Component *self, int width, int *line_count) {
    ImageData *d = (ImageData *)self->data;
    (void)width;

    if (!d->base64_data) {
        char **out = calloc(1, sizeof(char *));
        out[0] = strdup("[Image: no data]");
        *line_count = 1;
        return out;
    }

    int height = d->height_cells > 0 ? d->height_cells : 1;

    switch (d->protocol) {
    case IMG_PROTOCOL_KITTY: {
        /* Kitty graphics protocol:
         * \x1b_Gf=100,t=d,s=<w>,v=<h>,a=T;<base64>\x1b\\ */
        int data_len = (int)strlen(d->base64_data);
        int cap = data_len + 128;
        char *esc = malloc(cap);
        snprintf(esc, cap, "\x1b_Gf=100,t=d,s=%d,v=%d,a=T;%s\x1b\\",
                d->width_cells, d->height_cells, d->base64_data);

        char **out = calloc(height, sizeof(char *));
        out[0] = esc;
        for (int i = 1; i < height; i++) {
            out[i] = strdup("");
        }
        *line_count = height;
        return out;
    }

    case IMG_PROTOCOL_ITERM2: {
        /* iTerm2 inline images:
         * \x1b]1337;File=inline=1;width=<w>;height=<h>:<base64>\x07 */
        int data_len = (int)strlen(d->base64_data);
        int cap = data_len + 128;
        char *esc = malloc(cap);
        snprintf(esc, cap, "\x1b]1337;File=inline=1;width=%d;height=%d:%s\x07",
                d->width_cells, d->height_cells, d->base64_data);

        char **out = calloc(height, sizeof(char *));
        out[0] = esc;
        for (int i = 1; i < height; i++) {
            out[i] = strdup("");
        }
        *line_count = height;
        return out;
    }

    case IMG_PROTOCOL_SIXEL:
    case IMG_PROTOCOL_NONE:
    default: {
        /* Placeholder */
        int data_len = d->base64_data ? (int)strlen(d->base64_data) : 0;
        char *placeholder = malloc(128);
        snprintf(placeholder, 128, "[Image: %s %d bytes]",
                d->mime_type ? d->mime_type : "unknown",
                data_len);
        char **out = calloc(1, sizeof(char *));
        out[0] = placeholder;
        *line_count = 1;
        return out;
    }
    }
}

static void image_free(Component *self) {
    ImageData *d = (ImageData *)self->data;
    free(d->base64_data);
    free(d->mime_type);
    free(d);
}

ImageProtocol image_detect_protocol(void) {
    const char *term_program = getenv("TERM_PROGRAM");
    if (term_program) {
        if (strcmp(term_program, "iTerm.app") == 0 ||
            strcmp(term_program, "iTerm2") == 0) {
            return IMG_PROTOCOL_ITERM2;
        }
    }

    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "kitty") != NULL) {
            return IMG_PROTOCOL_KITTY;
        }
    }

    const char *kitty_window = getenv("KITTY_WINDOW_ID");
    if (kitty_window) {
        return IMG_PROTOCOL_KITTY;
    }

    return IMG_PROTOCOL_NONE;
}

Component *widget_image_create(const char *base64_data, const char *mime_type,
                                int width_cells, int height_cells) {
    Component *comp = calloc(1, sizeof(Component));
    if (!comp) return NULL;

    ImageData *d = calloc(1, sizeof(ImageData));
    if (!d) { free(comp); return NULL; }

    d->base64_data = base64_data ? strdup(base64_data) : NULL;
    d->mime_type = mime_type ? strdup(mime_type) : NULL;
    d->width_cells = width_cells;
    d->height_cells = height_cells;
    d->protocol = image_detect_protocol();

    comp->data = d;
    comp->render = image_render;
    comp->free_comp = image_free;

    return comp;
}

void widget_image_set_data(Component *comp, const char *base64_data, const char *mime_type) {
    if (!comp) return;
    ImageData *d = (ImageData *)comp->data;
    free(d->base64_data);
    free(d->mime_type);
    d->base64_data = base64_data ? strdup(base64_data) : NULL;
    d->mime_type = mime_type ? strdup(mime_type) : NULL;
}
