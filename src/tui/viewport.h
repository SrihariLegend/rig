#ifndef RIG_VIEWPORT_H
#define RIG_VIEWPORT_H

#include "linestore.h"
#include <stdbool.h>

typedef struct {
    LineStore *store;

    int term_width;
    int term_height;
    int content_width;

    int scroll_offset;
    bool auto_scroll;
    bool dirty;

    char *input_line;
    int input_cursor_pos;
    bool input_active;

    bool is_streaming;
    bool tool_breathing;
    int spinner_frame;
    char *spinner_tool_name;
} Viewport;

Viewport *viewport_create(LineStore *store);
void viewport_free(Viewport *vp);

void viewport_resize(Viewport *vp, int width, int height);
void viewport_render(Viewport *vp);
void viewport_render_full(Viewport *vp);

void viewport_set_input(Viewport *vp, const char *text, int cursor_pos);
void viewport_set_status(Viewport *vp, const char *left, const char *right);
void viewport_set_breathing(Viewport *vp, bool active, const char *tool_name);
void viewport_tick_spinner(Viewport *vp);

void viewport_scroll_up(Viewport *vp, int lines);
void viewport_scroll_down(Viewport *vp, int lines);
void viewport_scroll_to_bottom(Viewport *vp);
bool viewport_at_bottom(const Viewport *vp);

#endif
