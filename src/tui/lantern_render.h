#ifndef PI_LANTERN_RENDER_H
#define PI_LANTERN_RENDER_H

#include "lantern.h"
#include "linestore.h"
#include <stdbool.h>

typedef struct {
    Lantern *lantern;
    LineStore *store;

    int term_width;
    int term_height;
    int content_width;
    int left_margin;

    int scroll_offset;
    bool auto_scroll;
    bool dirty;

    char *input_line;
    int input_cursor_pos;
    bool input_active;
    bool show_scroll_hint;

    bool tool_breathing;
    bool is_streaming;
    int spinner_frame;
    char *spinner_tool_name;
} LanternRenderer;

LanternRenderer *lantern_renderer_create(Lantern *lantern, LineStore *store);
void lantern_renderer_free(LanternRenderer *r);

void lantern_renderer_resize(LanternRenderer *r, int width, int height);
void lantern_renderer_render(LanternRenderer *r);
void lantern_renderer_render_full(LanternRenderer *r);

void lantern_renderer_set_input(LanternRenderer *r, const char *text, int cursor_pos);
void lantern_renderer_set_breathing(LanternRenderer *r, bool breathing, const char *tool_name);
void lantern_renderer_tick_spinner(LanternRenderer *r);

void lantern_renderer_scroll_up(LanternRenderer *r, int lines);
void lantern_renderer_scroll_down(LanternRenderer *r, int lines);
void lantern_renderer_scroll_to_bottom(LanternRenderer *r);
bool lantern_renderer_at_bottom(const LanternRenderer *r);

#endif
