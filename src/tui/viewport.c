#include "viewport.h"
#include "terminal.h"
#include "ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define INPUT_ROWS 1
#define STATUS_ROWS 1
#define RESERVED_ROWS (INPUT_ROWS + STATUS_ROWS)

static const char *SPINNER[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
#define SPINNER_COUNT 10

Viewport *viewport_create(LineStore *store) {
    Viewport *vp = calloc(1, sizeof(Viewport));
    if (!vp) return NULL;
    vp->store = store;
    vp->auto_scroll = true;
    vp->dirty = true;
    vp->input_active = true;
    return vp;
}

void viewport_free(Viewport *vp) {
    if (!vp) return;
    free(vp->input_line);
    free(vp->spinner_tool_name);
    free(vp->status_left);
    free(vp->status_right);
    free(vp);
}

void viewport_resize(Viewport *vp, int width, int height) {
    vp->term_width = width;
    vp->term_height = height;
    vp->content_width = width - 4;
    if (vp->content_width < 20) vp->content_width = 20;
    if (vp->content_width > 100) vp->content_width = 100;

    linestore_set_width(vp->store, vp->content_width);
    linestore_reflow(vp->store);
    vp->dirty = true;
}

void viewport_set_input(Viewport *vp, const char *text, int cursor_pos) {
    free(vp->input_line);
    vp->input_line = text ? strdup(text) : NULL;
    vp->input_cursor_pos = cursor_pos;
    vp->dirty = true;
}

void viewport_set_status(Viewport *vp, const char *left, const char *right) {
    free(vp->status_left);
    free(vp->status_right);
    vp->status_left = left ? strdup(left) : NULL;
    vp->status_right = right ? strdup(right) : NULL;
    vp->dirty = true;
}

void viewport_set_breathing(Viewport *vp, bool active, const char *tool_name) {
    free(vp->spinner_tool_name);
    vp->spinner_tool_name = (active && tool_name) ? strdup(tool_name) : NULL;
    vp->is_streaming = active;
    vp->dirty = true;
}

void viewport_tick_spinner(Viewport *vp) {
    vp->spinner_frame = (vp->spinner_frame + 1) % SPINNER_COUNT;
    vp->dirty = true;
}

/* ---- Scroll ---- */

static int viewport_rows(const Viewport *vp) {
    return vp->term_height - RESERVED_ROWS;
}

void viewport_scroll_up(Viewport *vp, int lines) {
    vp->scroll_offset -= lines;
    if (vp->scroll_offset < 0) vp->scroll_offset = 0;
    vp->auto_scroll = false;
    vp->dirty = true;
}

void viewport_scroll_down(Viewport *vp, int lines) {
    int total = linestore_screen_row_count(vp->store);
    int max_offset = total - viewport_rows(vp);
    if (max_offset < 0) max_offset = 0;
    vp->scroll_offset += lines;
    if (vp->scroll_offset > max_offset) vp->scroll_offset = max_offset;
    if (vp->scroll_offset >= max_offset) vp->auto_scroll = true;
    vp->dirty = true;
}

void viewport_scroll_to_bottom(Viewport *vp) {
    int total = linestore_screen_row_count(vp->store);
    int max_offset = total - viewport_rows(vp);
    vp->scroll_offset = max_offset > 0 ? max_offset : 0;
    vp->auto_scroll = true;
    vp->dirty = true;
}

bool viewport_at_bottom(const Viewport *vp) {
    return vp->auto_scroll;
}

/* ---- Rendering ---- */

static void write_str(const char *s) {
    if (s) write(STDOUT_FILENO, s, strlen(s));
}

static void move_to(int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
    write(STDOUT_FILENO, buf, (size_t)n);
}

static void clear_line(void) {
    write_str("\033[2K");
}

static void render_span(const Span *span) {
    if (span->flags & SPAN_BOLD) write_str("\033[1m");
    if (span->flags & SPAN_ITALIC) write_str("\033[3m");
    if (span->flags & SPAN_STRIKE) write_str("\033[9m");
    if (span->flags & SPAN_CODE) write_str("\033[7m");
    if (span->flags & SPAN_ACCENT) write_str("\033[36m");
    write(STDOUT_FILENO, span->text, (size_t)span->len);
    write_str("\033[0m");
}

static void render_line(const Viewport *vp, const StoreLine *line, int row) {
    move_to(row, 1);
    clear_line();

    int margin = (vp->term_width - vp->content_width) / 2;
    if (margin < 1) margin = 1;
    char pad[128];
    int pn = margin < 127 ? margin : 127;
    memset(pad, ' ', (size_t)pn);
    pad[pn] = '\0';

    switch (line->type) {
    case LINE_BLANK:
        break;

    case LINE_USER_TEXT:
        write_str(pad);
        write_str("\033[36m> \033[0m");
        if (line->raw_text) write_str(line->raw_text);
        break;

    case LINE_ASSISTANT_TEXT:
        write_str(pad);
        if (line->spans && line->span_count > 0) {
            for (int i = 0; i < line->span_count; i++)
                render_span(&line->spans[i]);
        } else if (line->raw_text) {
            write_str(line->raw_text);
        }
        break;

    case LINE_HEADING:
        write_str(pad);
        write_str("\033[1;36m");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_CODE:
        write_str(pad);
        write_str("  \033[2m");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_CODE_LANG:
        write_str(pad);
        write_str("\033[2m───");
        if (line->raw_text) { write_str(" "); write_str(line->raw_text); }
        write_str("\033[0m");
        break;

    case LINE_BLOCKQUOTE:
        write_str(pad);
        write_str("\033[2m│ \033[0m\033[3m");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_LIST_ITEM:
        write_str(pad);
        if (line->indent > 0) {
            for (int i = 0; i < line->indent; i++) write_str("  ");
        }
        write_str("• ");
        if (line->spans && line->span_count > 0) {
            for (int i = 0; i < line->span_count; i++)
                render_span(&line->spans[i]);
        } else if (line->raw_text) {
            write_str(line->raw_text);
        }
        break;

    case LINE_TOOL_START:
        write_str(pad);
        write_str("\033[33m⚡ ");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_TOOL_OUTPUT:
        write_str(pad);
        write_str("  \033[2m");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_TOOL_DONE:
        write_str(pad);
        write_str("\033[32m✓ ");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_ERROR:
        write_str(pad);
        write_str("\033[31m✗ ");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_SYSTEM:
        write_str(pad);
        write_str("\033[2;3m");
        if (line->raw_text) write_str(line->raw_text);
        write_str("\033[0m");
        break;

    case LINE_SEPARATOR:
        write_str(pad);
        write_str("\033[2m");
        for (int i = 0; i < vp->content_width; i++) write_str("─");
        write_str("\033[0m");
        break;

    case LINE_TABLE_ROW:
    case LINE_TABLE_SEPARATOR:
        write_str(pad);
        if (line->raw_text) write_str(line->raw_text);
        break;

    case LINE_SPLASH:
        write_str(pad);
        if (line->raw_text) write_str(line->raw_text);
        break;
    }
}

static void render_status_bar(const Viewport *vp) {
    move_to(vp->term_height - 1, 1);
    clear_line();
    write_str("\033[7m");

    char bar[512];
    const char *left = vp->status_left ? vp->status_left : "";
    const char *right = vp->status_right ? vp->status_right : "";

    int left_len = (int)strlen(left);
    int right_len = (int)strlen(right);
    int pad_len = vp->term_width - left_len - right_len;
    if (pad_len < 1) pad_len = 1;

    int n = snprintf(bar, sizeof(bar), " %s%*s%s ", left, pad_len - 2, "", right);
    write(STDOUT_FILENO, bar, (size_t)(n < (int)sizeof(bar) ? n : (int)sizeof(bar) - 1));
    write_str("\033[0m");
}

static void render_input_line(const Viewport *vp) {
    move_to(vp->term_height, 1);
    clear_line();

    if (vp->is_streaming) {
        const char *frame = SPINNER[vp->spinner_frame % SPINNER_COUNT];
        write_str("\033[33m");
        write_str(frame);
        write_str(" ");
        if (vp->spinner_tool_name) write_str(vp->spinner_tool_name);
        else write_str("thinking...");
        write_str("\033[0m");
        return;
    }

    write_str("\033[36m❯\033[0m ");
    if (vp->input_line) write_str(vp->input_line);

    /* Position cursor */
    int cursor_col = vp->input_cursor_pos + 3;  /* "❯ " = 2 cols + 1-indexed */
    char pos[32];
    int pn = snprintf(pos, sizeof(pos), "\033[%d;%dH", vp->term_height, cursor_col);
    write(STDOUT_FILENO, pos, (size_t)pn);
}

void viewport_render(Viewport *vp) {
    if (!vp->dirty) return;
    viewport_render_full(vp);
}

void viewport_render_full(Viewport *vp) {
    vp->dirty = false;
    if (vp->term_width <= 0 || vp->term_height <= 0) return;

    int vp_rows = viewport_rows(vp);
    int total = linestore_screen_row_count(vp->store);

    /* Auto-scroll: keep bottom pinned */
    if (vp->auto_scroll) {
        int max_offset = total - vp_rows;
        vp->scroll_offset = max_offset > 0 ? max_offset : 0;
    }

    /* Hide cursor during render */
    write_str("\033[?25l");

    /* Render visible content lines */
    for (int row = 0; row < vp_rows; row++) {
        int screen_row_idx = vp->scroll_offset + row;
        if (screen_row_idx < total) {
            ScreenRowRef ref = linestore_row_to_line(vp->store, screen_row_idx);
            if (ref.line_index >= 0 && ref.line_index < vp->store->count) {
                StoreLine *line = &vp->store->lines[ref.line_index];
                /* For wrapped lines, only render the first wrap on the first row */
                if (ref.wrap_offset == 0) {
                    render_line(vp, line, row + 1);
                } else {
                    /* Continuation of wrapped line */
                    move_to(row + 1, 1);
                    clear_line();
                    int margin = (vp->term_width - vp->content_width) / 2;
                    if (margin < 1) margin = 1;
                    char pad[128];
                    int pn = margin < 127 ? margin : 127;
                    memset(pad, ' ', (size_t)pn);
                    pad[pn] = '\0';
                    write_str(pad);
                    /* Render wrapped portion */
                    if (line->raw_text) {
                        int offset = ref.wrap_offset * vp->content_width;
                        int remaining = (int)strlen(line->raw_text) - offset;
                        if (remaining > 0) {
                            int chunk = remaining > vp->content_width ? vp->content_width : remaining;
                            write(STDOUT_FILENO, line->raw_text + offset, (size_t)chunk);
                        }
                    }
                }
            }
        } else {
            /* Empty row below content */
            move_to(row + 1, 1);
            clear_line();
        }
    }

    render_status_bar(vp);
    render_input_line(vp);

    /* Show cursor */
    write_str("\033[?25h");
}
