#include "lantern_render.h"
#include "terminal.h"
#include "ansi.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_LINE_BUF 4096

static const char *SPINNER_FRAMES[] = { "|", "/", "-", "\\" };
#define SPINNER_COUNT 4

LanternRenderer *lantern_renderer_create(Lantern *lantern, LineStore *store) {
    LanternRenderer *r = calloc(1, sizeof(LanternRenderer));
    if (!r) return NULL;
    r->lantern = lantern;
    r->store = store;
    r->auto_scroll = true;
    r->dirty = true;
    r->input_active = true;
    return r;
}

void lantern_renderer_free(LanternRenderer *r) {
    if (!r) return;
    free(r->input_line);
    free(r->spinner_tool_name);
    free(r);
}

void lantern_renderer_resize(LanternRenderer *r, int width, int height) {
    r->term_width = width;
    r->term_height = height;

    r->content_width = width < 90 ? width - 4 : 86;
    if (r->content_width < 20) r->content_width = 20;
    r->left_margin = 2;

    linestore_set_width(r->store, r->content_width - 4);
    linestore_reflow(r->store);
    lantern_rebuild_lut(r->lantern, height);
    r->dirty = true;
}

static void emit_padding(int count) {
    if (count <= 0) return;
    char spaces[128];
    int n = count < 127 ? count : 127;
    memset(spaces, ' ', n);
    spaces[n] = '\0';
    terminal_write_str(spaces);
}

static void render_span_text(const Lantern *l, const char *text, int len,
                              SpanFlags flags, const RGB *base_color, int distance) {
    char fg_buf[32];
    char reset_buf[8];
    lantern_emit_reset(reset_buf, sizeof(reset_buf));

    if (flags & SPAN_CODE) {
        RGB cool = *base_color;
        cool.r = (uint8_t)(cool.r * 0.85f);
        cool.g = (uint8_t)(cool.g * 0.85f);
        cool.b = (uint8_t)(cool.b * 0.85f);
        lantern_emit_fg(l, &cool, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\x1b[7m");
        terminal_write(text, len);
        terminal_write_str(reset_buf);
        return;
    }

    if (flags & SPAN_ACCENT) {
        RGB accent = lantern_accent_at(l, distance);
        lantern_emit_fg(l, &accent, fg_buf, sizeof(fg_buf));
    } else {
        RGB color = *base_color;
        if (flags & SPAN_BOLD) {
            color.r = (uint8_t)(color.r + (255 - color.r) * 0.1f);
            color.g = (uint8_t)(color.g + (255 - color.g) * 0.1f);
            color.b = (uint8_t)(color.b + (255 - color.b) * 0.1f);
        }
        lantern_emit_fg(l, &color, fg_buf, sizeof(fg_buf));
    }

    terminal_write_str(fg_buf);
    if (flags & SPAN_BOLD) terminal_write_str("\x1b[1m");
    if (flags & SPAN_ITALIC) terminal_write_str("\x1b[3m");
    if (flags & SPAN_STRIKE) terminal_write_str("\x1b[9m");
    terminal_write(text, len);
    terminal_write_str(reset_buf);
}

static void render_line_content(const LanternRenderer *r, const StoreLine *line,
                                 const RGB *base_color, int distance, int avail_width) {
    (void)avail_width;
    const Lantern *l = r->lantern;
    char fg_buf[32];
    char reset_buf[8];
    lantern_emit_reset(reset_buf, sizeof(reset_buf));

    switch (line->type) {
    case LINE_BLANK:
        break;

    case LINE_USER_TEXT: {
        RGB accent = lantern_accent_at(l, distance);
        lantern_emit_fg(l, &accent, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("> ");
        terminal_write_str(reset_buf);
        lantern_emit_fg(l, base_color, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_SYSTEM: {
        RGB dim = rgb_lerp(*base_color, (RGB){40, 40, 40}, 0.5f);
        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\x1b[3m");
        terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_HEADING: {
        RGB accent = lantern_accent_at(l, distance);
        int hlevel = line->heading_level;
        if (hlevel < 1) hlevel = 1;

        if (hlevel <= 2) {
            /* h1/h2: accent color, bold, uppercase feel */
            lantern_emit_fg(l, &accent, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str("\x1b[1m");
        } else {
            /* h3+: bright text, bold only */
            RGB heading = *base_color;
            heading.r = (uint8_t)(heading.r + (255 - heading.r) * 0.12f);
            heading.g = (uint8_t)(heading.g + (255 - heading.g) * 0.12f);
            heading.b = (uint8_t)(heading.b + (255 - heading.b) * 0.12f);
            lantern_emit_fg(l, &heading, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str("\x1b[1m");
        }

        if (line->spans && line->span_count > 0) {
            for (int s = 0; s < line->span_count; s++) {
                terminal_write(line->spans[s].text, line->spans[s].len);
            }
        } else if (line->raw_text) {
            terminal_write_str(line->raw_text);
        }
        terminal_write_str(reset_buf);

        /* h1: underline rule */
        if (hlevel == 1) {
            terminal_write_str("\n");
            emit_padding(r->left_margin + 2 + line->indent);
            RGB dim_accent = { (uint8_t)(accent.r * 0.3f), (uint8_t)(accent.g * 0.3f), (uint8_t)(accent.b * 0.3f) };
            lantern_emit_fg(l, &dim_accent, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            for (int i = 0; i < 36; i++) terminal_write_str("\xe2\x94\x80");
            terminal_write_str(reset_buf);
        }
        /* h2: shorter dim underline */
        else if (hlevel == 2) {
            terminal_write_str("\n");
            emit_padding(r->left_margin + 2 + line->indent);
            RGB dim = rgb_lerp(*base_color, (RGB){30, 30, 30}, 0.7f);
            lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            for (int i = 0; i < 24; i++) terminal_write_str("\xe2\x94\x80");
            terminal_write_str(reset_buf);
        }
        break;
    }

    case LINE_CODE_LANG: {
        RGB dim = rgb_lerp(*base_color, (RGB){40, 40, 40}, 0.5f);
        RGB accent = lantern_accent_at(l, distance);
        RGB dim_accent = { (uint8_t)(accent.r * 0.3f), (uint8_t)(accent.g * 0.3f), (uint8_t)(accent.b * 0.3f) };
        lantern_emit_fg(l, &dim_accent, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\xe2\x94\x8c");
        terminal_write_str(reset_buf);
        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str(" ");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_CODE: {
        RGB code_color = *base_color;
        code_color.r = (uint8_t)(code_color.r * 0.85f);
        code_color.g = (uint8_t)(code_color.g * 0.85f);
        code_color.b = (uint8_t)(code_color.b * 0.85f);

        /* Accent left bar */
        RGB accent = lantern_accent_at(l, distance);
        RGB dim_accent = { (uint8_t)(accent.r * 0.3f), (uint8_t)(accent.g * 0.3f), (uint8_t)(accent.b * 0.3f) };
        lantern_emit_fg(l, &dim_accent, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\xe2\x94\x82");
        terminal_write_str(reset_buf);

        /* Code text */
        lantern_emit_fg(l, &code_color, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str(" ");
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_BLOCKQUOTE: {
        RGB dim = rgb_lerp(*base_color, (RGB){60, 60, 60}, 0.3f);
        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\x1b[2m\xe2\x94\x82 ");
        terminal_write_str(reset_buf);

        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\x1b[3m");
        terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_LIST_ITEM: {
        RGB accent = lantern_accent_at(l, distance);
        RGB bullet_color = { (uint8_t)(accent.r * 0.25f + base_color->r * 0.75f),
                             (uint8_t)(accent.g * 0.25f + base_color->g * 0.75f),
                             (uint8_t)(accent.b * 0.25f + base_color->b * 0.75f) };
        lantern_emit_fg(l, &bullet_color, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        if (line->heading_level > 0) {
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "%d. ", line->heading_level);
            terminal_write_str(num_buf);
        } else {
            terminal_write_str("\xc2\xb7 ");
        }
        terminal_write_str(reset_buf);

        if (line->spans && line->span_count > 0) {
            for (int s = 0; s < line->span_count; s++) {
                render_span_text(l, line->spans[s].text, line->spans[s].len,
                                line->spans[s].flags, base_color, distance);
            }
        } else {
            lantern_emit_fg(l, base_color, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str(line->raw_text);
            terminal_write_str(reset_buf);
        }
        break;
    }

    case LINE_TOOL_START: {
        RGB accent = lantern_accent_at(l, distance);
        lantern_emit_fg(l, &accent, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str(SPINNER_FRAMES[r->spinner_frame % SPINNER_COUNT]);
        terminal_write_str(" ");
        terminal_write_str(reset_buf);

        RGB dim = rgb_lerp(*base_color, (RGB){40, 40, 40}, 0.5f);
        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_TOOL_OUTPUT: {
        RGB dim = rgb_lerp(*base_color, (RGB){40, 40, 40}, 0.6f);
        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_TOOL_DONE: {
        RGB accent = lantern_accent_at(l, distance);
        lantern_emit_fg(l, &accent, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_ERROR: {
        RGB err = lantern_error_at(l, distance);
        lantern_emit_fg(l, &err, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\xc2\xb7 ");
        terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_TABLE_ROW: {
        /* Header rows (heading_level=1) get bold */
        if (line->heading_level) {
            lantern_emit_fg(l, base_color, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str("\x1b[1m");
        } else {
            lantern_emit_fg(l, base_color, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
        }
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_TABLE_SEPARATOR: {
        RGB dim = rgb_lerp(*base_color, (RGB){40, 40, 40}, 0.5f);
        lantern_emit_fg(l, &dim, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        if (line->raw_text) terminal_write_str(line->raw_text);
        terminal_write_str(reset_buf);
        break;
    }

    case LINE_SEPARATOR:
        break;

    default:
        if (line->spans && line->span_count > 0) {
            for (int s = 0; s < line->span_count; s++) {
                render_span_text(l, line->spans[s].text, line->spans[s].len,
                                line->spans[s].flags, base_color, distance);
            }
        } else if (line->raw_text) {
            lantern_emit_fg(l, base_color, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str(line->raw_text);
            terminal_write_str(reset_buf);
        }
        break;
    }
}

void lantern_renderer_render(LanternRenderer *r) {
    if (!r || !r->lantern || !r->store) return;

    int viewport_height = r->term_height;
    if (viewport_height < 1) return;

    int total_rows = linestore_screen_row_count(r->store);
    int input_rows = 1;
    int content_viewport = viewport_height - input_rows;
    if (content_viewport < 1) content_viewport = 1;

    static int render_count = 0;
    render_count++;
    if (render_count % 50 == 0 || total_rows != r->store->total_screen_rows) {
        LOG_DEBUG("render #%d: store_lines=%d total_rows=%d scroll=%d auto=%d",
                  render_count, r->store->count, total_rows, r->scroll_offset, r->auto_scroll);
    }

    if (r->store->needs_scroll_reset) {
        r->store->needs_scroll_reset = false;
        r->auto_scroll = true;
    }

    if (r->auto_scroll) {
        r->scroll_offset = total_rows - content_viewport;
    }
    if (r->scroll_offset < 0) r->scroll_offset = 0;
    if (total_rows > content_viewport && r->scroll_offset > total_rows - content_viewport) {
        r->scroll_offset = total_rows - content_viewport;
    }

    int center_row = content_viewport / 2;

    terminal_sync_begin();

    for (int screen_y = 0; screen_y < content_viewport; screen_y++) {
        terminal_move_cursor(screen_y + 1, 1);
        terminal_clear_line();

        int global_row = r->scroll_offset + screen_y;
        if (global_row < 0 || global_row >= total_rows) continue;

        ScreenRowRef ref = linestore_row_to_line(r->store, global_row);
        if (ref.line_index < 0 || ref.line_index >= r->store->count) continue;

        const StoreLine *line = &r->store->lines[ref.line_index];
        int distance_from_center = screen_y - center_row;
        if (distance_from_center < 0) distance_from_center = -distance_from_center;

        RGB base_color = lantern_fade_color(r->lantern, distance_from_center);

        if (line->type == LINE_ASSISTANT_TEXT) {
            base_color.r = (uint8_t)(base_color.r * 0.92f);
            base_color.g = (uint8_t)(base_color.g * 0.92f);
            base_color.b = (uint8_t)(base_color.b * 0.92f);
        }

        if (r->tool_breathing && line->type != LINE_TOOL_START &&
            line->type != LINE_TOOL_OUTPUT && line->type != LINE_TOOL_DONE) {
            base_color.r = (uint8_t)(base_color.r * 0.04f);
            base_color.g = (uint8_t)(base_color.g * 0.04f);
            base_color.b = (uint8_t)(base_color.b * 0.04f);
        }

        int line_indent = line->indent & 0x7FFF;
        int indent = r->left_margin + 2 + line_indent;
        emit_padding(indent);

        int avail_width = r->content_width - 4 - line_indent;
        if (avail_width < 10) avail_width = 10;

        if (ref.wrap_offset == 0) {
            render_line_content(r, line, &base_color, distance_from_center, avail_width);
        } else if (line->raw_text) {
            /* Word-wrap aware rendering for wrapped lines */
            char fg_buf[32];
            lantern_emit_fg(r->lantern, &base_color, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);

            /* Walk through text finding word-wrap break points */
            const char *text = line->raw_text;
            int text_len = (int)strlen(text);
            int row = 0;
            int pos = 0;
            while (pos < text_len && row <= ref.wrap_offset) {
                int row_start = pos;
                int row_end = pos;
                int vis = 0;
                int last_space = -1;
                while (row_end < text_len && vis < avail_width) {
                    if (text[row_end] == ' ') last_space = row_end;
                    unsigned char c = (unsigned char)text[row_end];
                    int bytes = 1;
                    if (c >= 0xF0) bytes = 4;
                    else if (c >= 0xE0) bytes = 3;
                    else if (c >= 0xC0) bytes = 2;
                    row_end += bytes;
                    vis++;
                }
                /* Break at last space if we exceeded width */
                if (row_end < text_len && last_space > row_start) {
                    row_end = last_space + 1;
                }
                if (row == ref.wrap_offset) {
                    int chunk = row_end - row_start;
                    /* Trim trailing space on wrapped lines */
                    while (chunk > 0 && text[row_start + chunk - 1] == ' ') chunk--;
                    if (chunk > 0) terminal_write(text + row_start, chunk);
                }
                pos = row_end;
                /* Skip leading spaces on continuation */
                while (pos < text_len && text[pos] == ' ') pos++;
                row++;
            }
            terminal_write_str("\x1b[0m");
        } else if (ref.wrap_offset == 0) {
            render_line_content(r, line, &base_color, distance_from_center, avail_width);
        }
    }

    /* Input line at bottom */
    terminal_move_cursor(viewport_height, 1);
    terminal_clear_line();
    emit_padding(r->left_margin + 2);

    {
        char fg_buf[32];
        if (r->is_streaming) {
            RGB accent = r->lantern->config.accent;
            lantern_emit_fg(r->lantern, &accent, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str(SPINNER_FRAMES[r->spinner_frame % SPINNER_COUNT]);
            terminal_write_str("\x1b[0m");
            RGB dim = rgb_lerp(r->lantern->config.warmth, (RGB){40,40,40}, 0.5f);
            lantern_emit_fg(r->lantern, &dim, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            if (r->tool_breathing && r->spinner_tool_name) {
                terminal_write_str(" ");
                terminal_write_str(r->spinner_tool_name);
            }
            terminal_write_str("\x1b[0m");
        } else {
            RGB accent = r->lantern->config.accent;
            lantern_emit_fg(r->lantern, &accent, fg_buf, sizeof(fg_buf));
            terminal_write_str(fg_buf);
            terminal_write_str("> ");
            terminal_write_str("\x1b[0m");

            if (r->input_line && r->input_line[0]) {
                RGB warm = r->lantern->config.warmth;
                lantern_emit_fg(r->lantern, &warm, fg_buf, sizeof(fg_buf));
                terminal_write_str(fg_buf);
                terminal_write_str(r->input_line);
                terminal_write_str("\x1b[0m");
            }
        }
    }

    /* Scroll hint */
    if (!r->auto_scroll && total_rows > content_viewport) {
        r->show_scroll_hint = true;
        char fg_buf[32];
        RGB accent = r->lantern->config.accent;
        RGB dim_accent = { (uint8_t)(accent.r * 0.3f), (uint8_t)(accent.g * 0.3f), (uint8_t)(accent.b * 0.3f) };
        terminal_move_cursor(viewport_height - 1, r->term_width - 8);
        lantern_emit_fg(r->lantern, &dim_accent, fg_buf, sizeof(fg_buf));
        terminal_write_str(fg_buf);
        terminal_write_str("\xe2\x86\x93 below");
        terminal_write_str("\x1b[0m");
    }

    /* Position cursor at input (after "> " prefix) */
    if (!r->is_streaming) {
        int cursor_col = r->left_margin + 2 + 2;
        if (r->input_line && r->input_cursor_pos >= 0) {
            /* Count display width up to cursor position */
            int vis = 0;
            const char *p = r->input_line;
            int chars = 0;
            while (*p && chars < r->input_cursor_pos) {
                unsigned char c = (unsigned char)*p;
                int bytes = 1;
                if (c >= 0xF0) bytes = 4;
                else if (c >= 0xE0) bytes = 3;
                else if (c >= 0xC0) bytes = 2;
                vis++;
                p += bytes;
                chars++;
            }
            cursor_col += vis;
        }
        terminal_move_cursor(viewport_height, cursor_col + 1);
        terminal_show_cursor();
    } else {
        terminal_hide_cursor();
    }

    terminal_sync_end();
    r->dirty = false;
}

void lantern_renderer_render_full(LanternRenderer *r) {
    terminal_clear_screen();
    lantern_renderer_render(r);
}

void lantern_renderer_set_input(LanternRenderer *r, const char *text, int cursor_pos) {
    free(r->input_line);
    r->input_line = text ? strdup(text) : NULL;
    r->input_cursor_pos = cursor_pos;
    r->dirty = true;
}

void lantern_renderer_set_breathing(LanternRenderer *r, bool breathing, const char *tool_name) {
    r->tool_breathing = breathing;
    free(r->spinner_tool_name);
    r->spinner_tool_name = tool_name ? strdup(tool_name) : NULL;
    r->dirty = true;
}

void lantern_renderer_tick_spinner(LanternRenderer *r) {
    r->spinner_frame = (r->spinner_frame + 1) % SPINNER_COUNT;
    if (r->tool_breathing) r->dirty = true;
}

void lantern_renderer_scroll_up(LanternRenderer *r, int lines) {
    r->auto_scroll = false;
    r->scroll_offset -= lines;
    if (r->scroll_offset < 0) r->scroll_offset = 0;
    r->dirty = true;
}

void lantern_renderer_scroll_down(LanternRenderer *r, int lines) {
    int total = linestore_screen_row_count(r->store);
    int content_vp = r->term_height - 1;
    int max_offset = total - content_vp;
    if (max_offset < 0) max_offset = 0;

    r->scroll_offset += lines;
    if (r->scroll_offset >= max_offset) {
        r->scroll_offset = max_offset;
        r->auto_scroll = true;
    }
    r->dirty = true;
}

void lantern_renderer_scroll_to_bottom(LanternRenderer *r) {
    int total = linestore_screen_row_count(r->store);
    int content_vp = r->term_height - 1;
    int max_offset = total - content_vp;
    if (max_offset < 0) max_offset = 0;
    r->scroll_offset = max_offset;
    r->auto_scroll = true;
    r->dirty = true;
}

bool lantern_renderer_at_bottom(const LanternRenderer *r) {
    return r->auto_scroll;
}
