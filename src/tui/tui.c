#include "tui.h"
#include "terminal.h"
#include "keys.h"
#include "ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>

static volatile sig_atomic_t g_winch_flag = 0;

static void sigwinch_handler(int sig) {
    (void)sig;
    g_winch_flag = 1;
}

TUI *tui_create(void) {
    TUI *tui = calloc(1, sizeof(TUI));
    if (!tui) return NULL;

    tui->component_capacity = 8;
    tui->components = calloc(tui->component_capacity, sizeof(Component *));
    tui->overlay_capacity = 4;
    tui->overlays = calloc(tui->overlay_capacity, sizeof(Overlay));

    terminal_get_size(&tui->width, &tui->height);
    tui->dirty = true;

    return tui;
}

static void free_lines(char **lines, int count) {
    if (!lines) return;
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

void tui_free(TUI *tui) {
    if (!tui) return;
    free(tui->components);
    free(tui->overlays);
    free_lines(tui->previous_lines, tui->previous_line_count);
    free(tui);
}

void tui_add_component(TUI *tui, Component *comp) {
    if (!tui || !comp) return;

    if (tui->component_count >= tui->component_capacity) {
        int new_cap = tui->component_capacity * 2;
        Component **new_comps = realloc(tui->components, new_cap * sizeof(Component *));
        if (!new_comps) return;
        tui->components = new_comps;
        tui->component_capacity = new_cap;
    }

    tui->components[tui->component_count++] = comp;
    tui->dirty = true;
}

void tui_remove_component(TUI *tui, Component *comp) {
    if (!tui || !comp) return;
    for (int i = 0; i < tui->component_count; i++) {
        if (tui->components[i] == comp) {
            memmove(&tui->components[i], &tui->components[i + 1],
                    (tui->component_count - i - 1) * sizeof(Component *));
            tui->component_count--;
            tui->dirty = true;
            return;
        }
    }
}

static char **render_all(TUI *tui, int *total_lines) {
    int capacity = tui->height;
    char **lines = calloc(capacity, sizeof(char *));
    int count = 0;

    for (int c = 0; c < tui->component_count; c++) {
        if (!tui->components[c]->render) continue;

        int comp_lines = 0;
        char **comp_output = tui->components[c]->render(
            tui->components[c], tui->width, &comp_lines);

        if (!comp_output) continue;

        for (int i = 0; i < comp_lines && count < tui->height; i++) {
            if (count >= capacity) {
                capacity *= 2;
                lines = realloc(lines, capacity * sizeof(char *));
            }
            lines[count++] = comp_output[i] ? strdup(comp_output[i]) : strdup("");
        }

        for (int i = 0; i < comp_lines; i++) free(comp_output[i]);
        free(comp_output);
    }

    while (count < tui->height) {
        if (count >= capacity) {
            capacity *= 2;
            lines = realloc(lines, capacity * sizeof(char *));
        }
        lines[count++] = strdup("");
    }

    *total_lines = count;
    return lines;
}

static void overlay_position(OverlayAnchor anchor, int term_w, int term_h,
                             int ov_w, int ov_h, int *out_row, int *out_col) {
    int row = 0, col = 0;

    switch (anchor) {
        case ANCHOR_TOP_LEFT:      row = 0;                       col = 0;                       break;
        case ANCHOR_TOP_CENTER:    row = 0;                       col = (term_w - ov_w) / 2;     break;
        case ANCHOR_TOP_RIGHT:     row = 0;                       col = term_w - ov_w;           break;
        case ANCHOR_CENTER_LEFT:   row = (term_h - ov_h) / 2;    col = 0;                       break;
        case ANCHOR_CENTER:        row = (term_h - ov_h) / 2;    col = (term_w - ov_w) / 2;     break;
        case ANCHOR_CENTER_RIGHT:  row = (term_h - ov_h) / 2;    col = term_w - ov_w;           break;
        case ANCHOR_BOTTOM_LEFT:   row = term_h - ov_h;          col = 0;                       break;
        case ANCHOR_BOTTOM_CENTER: row = term_h - ov_h;          col = (term_w - ov_w) / 2;     break;
        case ANCHOR_BOTTOM_RIGHT:  row = term_h - ov_h;          col = term_w - ov_w;           break;
    }

    if (row < 0) row = 0;
    if (col < 0) col = 0;
    *out_row = row;
    *out_col = col;
}

static void composite_overlays(TUI *tui, char **lines, int line_count) {
    if (!tui || !lines) return;

    for (int o = 0; o < tui->overlay_count; o++) {
        Overlay *ov = &tui->overlays[o];
        if (!ov->component || !ov->component->render) continue;

        int ov_w = ov->width > 0 ? ov->width : tui->width / 2;
        int ov_lines = 0;
        char **ov_output = ov->component->render(ov->component, ov_w, &ov_lines);
        if (!ov_output || ov_lines == 0) {
            if (ov_output) free(ov_output);
            continue;
        }

        int ov_h = ov->height > 0 ? ov->height : ov_lines;
        if (ov_h > ov_lines) ov_h = ov_lines;

        int row, col;
        overlay_position(ov->anchor, tui->width, tui->height, ov_w, ov_h, &row, &col);

        row += ov->margin_top;
        col += ov->margin_left;

        for (int i = 0; i < ov_h && (row + i) < line_count; i++) {
            if (!ov_output[i]) continue;

            const char *overlay_text = ov_output[i];
            int overlay_vis_len = ansi_strip_len(overlay_text);

            /* Build a new line: prefix from base + overlay + suffix from base */
            const char *base = lines[row + i] ? lines[row + i] : "";
            int base_vis_len = ansi_strip_len(base);

            /* For simplicity: pad base if col > base visible length */
            int buf_size = (int)strlen(base) + (int)strlen(overlay_text) + col + 16;
            char *merged = calloc(buf_size, 1);
            if (!merged) continue;

            /* Copy characters from base up to col visible chars */
            int vis = 0, src = 0;
            while (base[src] && vis < col) {
                if (base[src] == '\x1b') {
                    /* Copy entire ANSI sequence */
                    while (base[src] && base[src] != 'm' &&
                           !(base[src] >= 'A' && base[src] <= 'Z' && base[src] != '[')) {
                        merged[strlen(merged)] = base[src++];
                    }
                    if (base[src]) merged[strlen(merged)] = base[src++];
                } else {
                    merged[strlen(merged)] = base[src++];
                    vis++;
                }
            }

            /* Pad with spaces if base is shorter than col */
            while (vis < col) {
                strcat(merged, " ");
                vis++;
            }

            /* Append overlay content */
            strcat(merged, overlay_text);
            int after_overlay = col + overlay_vis_len;

            /* Skip base characters that are covered by the overlay */
            vis = 0;
            int skip_src = 0;
            while (base[skip_src] && vis < after_overlay) {
                if (base[skip_src] == '\x1b') {
                    while (base[skip_src] && base[skip_src] != 'm' &&
                           !(base[skip_src] >= 'A' && base[skip_src] <= 'Z' && base[skip_src] != '[')) {
                        skip_src++;
                    }
                    if (base[skip_src]) skip_src++;
                } else {
                    skip_src++;
                    vis++;
                }
            }

            /* Append remainder of base line after overlay */
            if (base[skip_src] && (int)base_vis_len > after_overlay) {
                strcat(merged, &base[skip_src]);
            }

            free(lines[row + i]);
            lines[row + i] = merged;
        }

        for (int i = 0; i < ov_lines; i++) free(ov_output[i]);
        free(ov_output);
    }
}

void tui_render(TUI *tui) {
    if (!tui) return;

    int new_count = 0;
    char **new_lines = render_all(tui, &new_count);

    composite_overlays(tui, new_lines, new_count);

    terminal_sync_begin();

    int first_changed = -1;
    int last_changed = -1;

    int max_lines = new_count < tui->previous_line_count ? tui->previous_line_count : new_count;

    for (int i = 0; i < max_lines; i++) {
        const char *old_line = (i < tui->previous_line_count && tui->previous_lines[i])
            ? tui->previous_lines[i] : "";
        const char *new_line = (i < new_count && new_lines[i]) ? new_lines[i] : "";

        if (strcmp(old_line, new_line) != 0) {
            if (first_changed == -1) first_changed = i;
            last_changed = i;
        }
    }

    if (first_changed >= 0) {
        for (int i = first_changed; i <= last_changed && i < new_count; i++) {
            terminal_move_cursor(i + 1, 1);
            terminal_clear_line();
            if (new_lines[i]) {
                terminal_write_str(new_lines[i]);
            }
        }
    }

    terminal_sync_end();

    free_lines(tui->previous_lines, tui->previous_line_count);
    tui->previous_lines = new_lines;
    tui->previous_line_count = new_count;
    tui->dirty = false;
}

void tui_render_full(TUI *tui) {
    if (!tui) return;

    free_lines(tui->previous_lines, tui->previous_line_count);
    tui->previous_lines = NULL;
    tui->previous_line_count = 0;

    terminal_clear_screen();
    tui_render(tui);
}

void tui_add_overlay(TUI *tui, Component *comp, OverlayAnchor anchor) {
    if (!tui || !comp) return;

    if (tui->overlay_count >= tui->overlay_capacity) {
        int new_cap = tui->overlay_capacity * 2;
        Overlay *new_overlays = realloc(tui->overlays, new_cap * sizeof(Overlay));
        if (!new_overlays) return;
        tui->overlays = new_overlays;
        tui->overlay_capacity = new_cap;
    }

    tui->overlays[tui->overlay_count++] = (Overlay){
        .component = comp,
        .anchor = anchor,
    };
    tui->dirty = true;
}

void tui_remove_overlay(TUI *tui, Component *comp) {
    if (!tui || !comp) return;
    for (int i = 0; i < tui->overlay_count; i++) {
        if (tui->overlays[i].component == comp) {
            memmove(&tui->overlays[i], &tui->overlays[i + 1],
                    (tui->overlay_count - i - 1) * sizeof(Overlay));
            tui->overlay_count--;
            tui->dirty = true;
            return;
        }
    }
}

void tui_invalidate(TUI *tui) {
    if (tui) tui->dirty = true;
}

void tui_resize(TUI *tui) {
    if (!tui) return;
    terminal_get_size(&tui->width, &tui->height);
    tui->dirty = true;
}

void tui_quit(TUI *tui) {
    if (tui) tui->running = false;
}

void tui_set_key_handler(TUI *tui, TuiKeyHandler handler, void *ctx) {
    if (!tui) return;
    tui->key_handler = handler;
    tui->key_handler_ctx = ctx;
}

int tui_run(TUI *tui) {
    if (!tui) return -1;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: Interactive mode requires a terminal.\n"
                        "Use -p for print mode: pi -p \"your prompt\"\n");
        return -1;
    }

    /* Install SIGWINCH handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    /* Enter raw mode and enable terminal protocols */
    terminal_enter_raw_mode();
    terminal_enable_kitty_keyboard();
    terminal_enable_bracketed_paste();
    terminal_enable_mouse();
    terminal_hide_cursor();
    terminal_clear_screen();

    tui->running = true;
    tui->dirty = true;

    /* Initial render */
    tui_render_full(tui);

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    while (tui->running) {
        /* Check for terminal resize */
        if (g_winch_flag) {
            g_winch_flag = 0;
            tui_resize(tui);
            tui_render_full(tui);
        }

        int ready = poll(&pfd, 1, 16); /* 16ms = ~60fps */

        if (ready > 0 && (pfd.revents & POLLIN)) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                ParsedKey key = key_parse(buf, (int)n);

                /* Let external handler process first */
                bool consumed = false;
                if (tui->key_handler) {
                    consumed = tui->key_handler(tui, &key, tui->key_handler_ctx);
                    if (!tui->running) break;
                }
                if (!consumed && !tui->key_handler) {
                    /* Default: exit on ctrl+c or escape (only when no handler) */
                    if (key_matches(&key, "ctrl+c") || key_matches(&key, "escape")) {
                        tui->running = false;
                        break;
                    }
                }

                if (consumed) goto render_check;

                /* Dispatch to focused component */
                for (int i = 0; i < tui->component_count; i++) {
                    Component *comp = tui->components[i];
                    if (comp->focused && comp->handle_input) {
                        comp->handle_input(comp, buf, (int)n);
                        tui->dirty = true;
                    }
                }

                /* Also dispatch to focused overlays */
                for (int i = 0; i < tui->overlay_count; i++) {
                    Component *comp = tui->overlays[i].component;
                    if (comp && comp->focused && comp->handle_input) {
                        comp->handle_input(comp, buf, (int)n);
                        tui->dirty = true;
                    }
                }

                render_check: (void)0;
            }
        }

        /* Render if dirty */
        if (tui->dirty) {
            tui_render(tui);
        }
    }

    /* Cleanup: disable protocols, exit raw mode, show cursor */
    terminal_disable_mouse();
    terminal_disable_bracketed_paste();
    terminal_disable_kitty_keyboard();
    terminal_show_cursor();
    terminal_exit_raw_mode();

    return 0;
}

void component_free(Component *comp) {
    if (!comp) return;
    if (comp->free_comp) comp->free_comp(comp);
    free(comp);
}
