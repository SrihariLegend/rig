#include "viewport.h"
#include "terminal.h"
#include "ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define LEFT_PAD 2
#define INPUT_ROWS 1

/* Amber palette — warm gold tones */
#define FG_MAIN    "\033[38;2;200;180;140m"
#define FG_DIM     "\033[38;2;120;108;84m"
#define FG_ACCENT  "\033[38;2;220;160;60m"
#define FG_BRIGHT  "\033[38;2;240;220;180m"
#define FG_ERROR   "\033[38;2;220;80;60m"
#define FG_GREEN   "\033[38;2;100;180;100m"
#define FG_CODE    "\033[38;2;170;155;120m"
#define RST        "\033[0m"

static const char *SPINNER[] = {"|", "/", "-", "\\"};
#define SPINNER_COUNT 4

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
    free(vp);
}

void viewport_resize(Viewport *vp, int width, int height) {
    vp->term_width = width;
    vp->term_height = height;
    vp->content_width = width - LEFT_PAD - 2;
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
    (void)vp; (void)left; (void)right;
}

void viewport_set_breathing(Viewport *vp, bool active, const char *tool_name) {
    vp->tool_breathing = active;
    free(vp->spinner_tool_name);
    vp->spinner_tool_name = (active && tool_name) ? strdup(tool_name) : NULL;
    vp->dirty = true;
}

void viewport_tick_spinner(Viewport *vp) {
    if (vp->tool_breathing || vp->is_streaming) {
        vp->spinner_frame = (vp->spinner_frame + 1) % SPINNER_COUNT;
        vp->dirty = true;
    }
}

/* ---- Scroll ---- */

static int content_rows(const Viewport *vp) {
    return vp->term_height - INPUT_ROWS;
}

void viewport_scroll_up(Viewport *vp, int lines) {
    vp->scroll_offset -= lines;
    if (vp->scroll_offset < 0) vp->scroll_offset = 0;
    vp->auto_scroll = false;
    vp->dirty = true;
}

void viewport_scroll_down(Viewport *vp, int lines) {
    int total = linestore_screen_row_count(vp->store);
    int max_offset = total - content_rows(vp);
    if (max_offset < 0) max_offset = 0;
    vp->scroll_offset += lines;
    if (vp->scroll_offset >= max_offset) {
        vp->scroll_offset = max_offset;
        vp->auto_scroll = true;
    }
    vp->dirty = true;
}

void viewport_scroll_to_bottom(Viewport *vp) {
    int total = linestore_screen_row_count(vp->store);
    int max_offset = total - content_rows(vp);
    vp->scroll_offset = max_offset > 0 ? max_offset : 0;
    vp->auto_scroll = true;
    vp->dirty = true;
}

bool viewport_at_bottom(const Viewport *vp) {
    return vp->auto_scroll;
}

/* ---- Rendering helpers ---- */

static void emit_pad(void) {
    char spaces[LEFT_PAD + 1];
    memset(spaces, ' ', LEFT_PAD);
    spaces[LEFT_PAD] = '\0';
    terminal_write_str(spaces);
}

static void emit_indent(int n) {
    for (int i = 0; i < n; i++) terminal_write_str("  ");
}

static void render_span_text(const Span *span) {
    if (span->flags & SPAN_BOLD) terminal_write_str("\033[1m");
    if (span->flags & SPAN_ITALIC) terminal_write_str("\033[3m");
    if (span->flags & SPAN_STRIKE) terminal_write_str("\033[9m");
    if (span->flags & SPAN_CODE) {
        terminal_write_str(FG_CODE "\033[7m");
        terminal_write(span->text, span->len);
        terminal_write_str(RST FG_MAIN);
        return;
    }
    if (span->flags & SPAN_ACCENT) terminal_write_str(FG_ACCENT);
    terminal_write(span->text, span->len);
    if (span->flags & (SPAN_BOLD | SPAN_ITALIC | SPAN_STRIKE | SPAN_ACCENT))
        terminal_write_str(RST FG_MAIN);
}

/* Render wrapped segment of raw_text using word-break logic */
static void render_wrap_segment(const char *text, int wrap_offset, int width) {
    int text_len = (int)strlen(text);
    int row = 0;
    int pos = 0;

    while (pos < text_len && row <= wrap_offset) {
        int row_start = pos;
        int row_end = pos;
        int vis = 0;
        int last_space = -1;

        while (row_end < text_len && vis < width) {
            if (text[row_end] == ' ') last_space = row_end;
            unsigned char c = (unsigned char)text[row_end];
            int bytes = 1;
            if (c >= 0xF0) bytes = 4;
            else if (c >= 0xE0) bytes = 3;
            else if (c >= 0xC0) bytes = 2;
            row_end += bytes;
            vis++;
        }
        if (row_end < text_len && last_space > row_start) {
            row_end = last_space + 1;
        }
        if (row == wrap_offset) {
            int chunk = row_end - row_start;
            while (chunk > 0 && text[row_start + chunk - 1] == ' ') chunk--;
            if (chunk > 0) terminal_write(text + row_start, chunk);
            return;
        }
        pos = row_end;
        while (pos < text_len && text[pos] == ' ') pos++;
        row++;
    }
}

static void render_line_first(const Viewport *vp, const StoreLine *line) {
    (void)vp;
    switch (line->type) {
    case LINE_BLANK:
        break;

    case LINE_USER_TEXT:
        terminal_write_str(FG_ACCENT "> " RST FG_BRIGHT);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_ASSISTANT_TEXT:
        terminal_write_str(FG_MAIN);
        if (line->spans && line->span_count > 0) {
            for (int i = 0; i < line->span_count; i++)
                render_span_text(&line->spans[i]);
        } else if (line->raw_text) {
            terminal_write_str(line->raw_text);
        }
        terminal_write_str(RST);
        break;

    case LINE_HEADING:
        terminal_write_str(FG_ACCENT "\033[1m");
        if (line->spans && line->span_count > 0) {
            for (int i = 0; i < line->span_count; i++)
                terminal_write(line->spans[i].text, line->spans[i].len);
        } else if (line->raw_text) {
            terminal_write_str(line->raw_text);
        }
        terminal_write_str(RST);
        break;

    case LINE_CODE_LANG:
        terminal_write_str(FG_DIM "\xe2\x94\x8c ");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_CODE:
        terminal_write_str(FG_DIM "\xe2\x94\x82 " RST FG_CODE);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_BLOCKQUOTE:
        terminal_write_str(FG_DIM "\xe2\x94\x82 \033[3m");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_LIST_ITEM:
        emit_indent(line->indent);
        if (line->heading_level > 0) {
            char num[16];
            snprintf(num, sizeof(num), "%d. ", line->heading_level);
            terminal_write_str(FG_ACCENT);
            terminal_write_str(num);
            terminal_write_str(RST FG_MAIN);
        } else {
            terminal_write_str(FG_ACCENT "\xc2\xb7 " RST FG_MAIN);
        }
        if (line->spans && line->span_count > 0) {
            for (int i = 0; i < line->span_count; i++)
                render_span_text(&line->spans[i]);
        } else if (line->raw_text) {
            terminal_write_str(line->raw_text);
        }
        terminal_write_str(RST);
        break;

    case LINE_TOOL_START:
        terminal_write_str(FG_ACCENT);
        terminal_write_str(SPINNER[vp->spinner_frame % SPINNER_COUNT]);
        terminal_write_str(" " RST FG_DIM);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_TOOL_OUTPUT:
        terminal_write_str(FG_DIM);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_TOOL_DONE:
        terminal_write_str(FG_GREEN "\xe2\x9c\x93 ");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_ERROR:
        terminal_write_str(FG_ERROR "\xc2\xb7 ");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_SYSTEM:
        terminal_write_str(FG_DIM "\033[3m");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_SEPARATOR:
        terminal_write_str(FG_DIM);
        for (int i = 0; i < 40; i++) terminal_write_str("\xe2\x94\x80");
        terminal_write_str(RST);
        break;

    case LINE_TABLE_ROW:
        terminal_write_str(FG_MAIN);
        if (line->heading_level) terminal_write_str("\033[1m");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_TABLE_SEPARATOR:
        terminal_write_str(FG_DIM);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(RST);
        break;

    case LINE_SPLASH:
        if (line->spans && line->span_count > 0) {
            for (int s = 0; s < line->span_count; s++) {
                float br = line->spans[s].brightness;
                if (br <= 0.001f) {
                    const unsigned char *p = (const unsigned char *)line->spans[s].text;
                    const unsigned char *end = p + line->spans[s].len;
                    while (p < end) {
                        terminal_write_str(" ");
                        if (*p >= 0xF0) p += 4;
                        else if (*p >= 0xE0) p += 3;
                        else if (*p >= 0xC0) p += 2;
                        else p++;
                    }
                } else {
                    int r = (int)(220 * br * 0.9f);
                    int g = (int)(160 * br * 0.9f);
                    int b = (int)(60 * br * 0.9f);
                    char fg[32];
                    snprintf(fg, sizeof(fg), "\033[38;2;%d;%d;%dm", r, g, b);
                    terminal_write_str(fg);
                    terminal_write(line->spans[s].text, line->spans[s].len);
                }
            }
            terminal_write_str(RST);
        } else {
            float br = line->brightness;
            if (br <= 0.0f) br = 0.01f;
            int r = (int)(220 * br);
            int g = (int)(160 * br);
            int b = (int)(60 * br);
            char fg[32];
            snprintf(fg, sizeof(fg), "\033[38;2;%d;%d;%dm", r, g, b);
            terminal_write_str(fg);
            if (line->raw_text) terminal_write_str(line->raw_text);
            terminal_write_str(RST);
        }
        break;
    }
}

/* ---- Main render ---- */

void viewport_render(Viewport *vp) {
    /* Always render when called — caller already checked dirty/needs_render */
    viewport_render_full(vp);
}

void viewport_render_full(Viewport *vp) {
    vp->dirty = false;
    if (vp->term_width <= 0 || vp->term_height <= 0) return;

    int vp_rows = content_rows(vp);
    int total = linestore_screen_row_count(vp->store);

    if (vp->store->needs_scroll_reset) {
        vp->store->needs_scroll_reset = false;
        vp->auto_scroll = true;
    }

    if (vp->auto_scroll) {
        vp->scroll_offset = total - vp_rows;
    }
    if (vp->scroll_offset < 0) vp->scroll_offset = 0;
    if (total > vp_rows && vp->scroll_offset > total - vp_rows) {
        vp->scroll_offset = total - vp_rows;
    }

    terminal_sync_begin();

    for (int screen_y = 0; screen_y < vp_rows; screen_y++) {
        terminal_move_cursor(screen_y + 1, 1);
        terminal_clear_line();

        int global_row = vp->scroll_offset + screen_y;
        if (global_row < 0 || global_row >= total) continue;

        ScreenRowRef ref = linestore_row_to_line(vp->store, global_row);
        if (ref.line_index < 0 || ref.line_index >= vp->store->count) continue;

        const StoreLine *line = &vp->store->lines[ref.line_index];

        /* Dim non-tool lines during tool breathing */
        bool dimmed = vp->tool_breathing &&
            line->type != LINE_TOOL_START &&
            line->type != LINE_TOOL_OUTPUT &&
            line->type != LINE_TOOL_DONE;

        if (dimmed) terminal_write_str("\033[2m");

        int indent = LEFT_PAD + (line->indent & 0x7FFF);
        emit_pad();
        if ((line->indent & 0x7FFF) > 0 && line->type != LINE_LIST_ITEM)
            emit_indent(line->indent & 0x7FFF);

        if (ref.wrap_offset == 0) {
            render_line_first(vp, line);
        } else if (line->raw_text) {
            /* Wrapped continuation */
            terminal_write_str(FG_MAIN);
            render_wrap_segment(line->raw_text, ref.wrap_offset, vp->content_width - (indent - LEFT_PAD));
            terminal_write_str(RST);
        }

        if (dimmed) terminal_write_str(RST);
    }

    /* Input line at bottom */
    terminal_move_cursor(vp->term_height, 1);
    terminal_clear_line();
    emit_pad();

    if (vp->is_streaming || vp->tool_breathing) {
        terminal_write_str(FG_ACCENT);
        terminal_write_str(SPINNER[vp->spinner_frame % SPINNER_COUNT]);
        terminal_write_str(RST " " FG_DIM);
        if (vp->tool_breathing && vp->spinner_tool_name) {
            terminal_write_str(vp->spinner_tool_name);
        } else {
            terminal_write_str("...");
        }
        terminal_write_str(RST);
        terminal_hide_cursor();
    } else {
        terminal_write_str(FG_ACCENT "> " RST);
        if (vp->input_line && vp->input_line[0]) {
            terminal_write_str(FG_BRIGHT);
            terminal_write_str(vp->input_line);
            terminal_write_str(RST);
        }

        /* Position cursor */
        int cursor_col = LEFT_PAD + 2 + 1;
        if (vp->input_line && vp->input_cursor_pos > 0) {
            const char *p = vp->input_line;
            int chars = 0;
            while (*p && chars < vp->input_cursor_pos) {
                unsigned char c = (unsigned char)*p;
                int bytes = 1;
                if (c >= 0xF0) bytes = 4;
                else if (c >= 0xE0) bytes = 3;
                else if (c >= 0xC0) bytes = 2;
                p += bytes;
                chars++;
                cursor_col++;
            }
        }
        terminal_move_cursor(vp->term_height, cursor_col);
        terminal_show_cursor();
    }

    /* Scroll hint */
    if (!vp->auto_scroll && total > vp_rows) {
        terminal_move_cursor(vp->term_height - 1, vp->term_width - 8);
        terminal_write_str(FG_DIM "\xe2\x86\x93 below" RST);
    }

    terminal_sync_end();
}
