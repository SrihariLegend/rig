#ifndef PI_WIDGET_IMAGE_H
#define PI_WIDGET_IMAGE_H

#include "tui/tui.h"

typedef enum {
    IMG_PROTOCOL_KITTY,
    IMG_PROTOCOL_ITERM2,
    IMG_PROTOCOL_SIXEL,
    IMG_PROTOCOL_NONE,
} ImageProtocol;

Component *widget_image_create(const char *base64_data, const char *mime_type,
                                int width_cells, int height_cells);
void widget_image_set_data(Component *comp, const char *base64_data, const char *mime_type);

ImageProtocol image_detect_protocol(void);

#endif
