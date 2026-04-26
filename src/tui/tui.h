#ifndef PI_TUI_H
#define PI_TUI_H

#include <stdbool.h>

typedef struct TUI TUI;

typedef struct Component Component;
struct Component {
    char **(*render)(Component *self, int width, int *line_count);
    void (*handle_input)(Component *self, const char *data, int len);
    void (*invalidate)(Component *self);
    void (*free_comp)(Component *self);
    bool wants_key_release;
    bool focused;
    void *data;
};

typedef enum {
    ANCHOR_TOP_LEFT, ANCHOR_TOP_CENTER, ANCHOR_TOP_RIGHT,
    ANCHOR_CENTER_LEFT, ANCHOR_CENTER, ANCHOR_CENTER_RIGHT,
    ANCHOR_BOTTOM_LEFT, ANCHOR_BOTTOM_CENTER, ANCHOR_BOTTOM_RIGHT,
} OverlayAnchor;

typedef struct {
    Component *component;
    OverlayAnchor anchor;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
    int width;
    int height;
} Overlay;

#include "keys.h"
// Return true to consume the key (don't dispatch to widgets)
typedef bool (*TuiKeyHandler)(TUI *tui, const ParsedKey *key, void *ctx);
typedef void (*TuiTickHandler)(TUI *tui, void *ctx);

struct TUI {
    Component **components;
    int component_count;
    int component_capacity;
    char **previous_lines;
    int previous_line_count;
    Overlay *overlays;
    int overlay_count;
    int overlay_capacity;
    char **virtual_lines;
    int virtual_line_count;
    int scroll_offset;
    bool auto_scroll;
    int width;
    int height;
    bool dirty;
    bool running;
    TuiKeyHandler key_handler;
    void *key_handler_ctx;
    TuiTickHandler tick_handler;
    void *tick_handler_ctx;
};

TUI *tui_create(void);
void tui_free(TUI *tui);

void tui_add_component(TUI *tui, Component *comp);
void tui_remove_component(TUI *tui, Component *comp);

void tui_render(TUI *tui);
void tui_render_full(TUI *tui);

void tui_add_overlay(TUI *tui, Component *comp, OverlayAnchor anchor);
void tui_remove_overlay(TUI *tui, Component *comp);

void tui_invalidate(TUI *tui);
void tui_resize(TUI *tui);

int tui_run(TUI *tui);
void tui_quit(TUI *tui);
void tui_set_key_handler(TUI *tui, TuiKeyHandler handler, void *ctx);
void tui_set_tick_handler(TUI *tui, TuiTickHandler handler, void *ctx);

void component_free(Component *comp);

#endif
