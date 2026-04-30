#include "selector.h"
#include "terminal.h"
#include "keys.h"
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>

int tui_selector(const char **items, int count, int initial) {
    if (!items || count <= 0) return -1;
    int sel = (initial >= 0 && initial < count) ? initial : 0;

    int cols, rows;
    terminal_get_size(&cols, &rows);
    int visible = count < (rows - 2) ? count : (rows - 2);

    /* Hide cursor */
    write(STDOUT_FILENO, "\033[?25l", 6);

    for (;;) {
        /* Render list */
        for (int i = 0; i < visible; i++) {
            if (i == sel) {
                /* Highlight: reverse video */
                char buf[512];
                int n = snprintf(buf, sizeof(buf), "\033[7m  %.*s  \033[0m\r\n",
                                 (int)(sizeof(buf) - 20), items[i]);
                write(STDOUT_FILENO, buf, (size_t)n);
            } else {
                char buf[512];
                int n = snprintf(buf, sizeof(buf), "  %.*s  \r\n",
                                 (int)(sizeof(buf) - 10), items[i]);
                write(STDOUT_FILENO, buf, (size_t)n);
            }
        }

        /* Read key */
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        int ready = poll(&pfd, 1, -1);
        if (ready <= 0) break;

        char buf[64];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';

        ParsedKey key = key_parse(buf, (int)n);

        if (key_matches(&key, "up")) {
            if (sel > 0) sel--;
        } else if (key_matches(&key, "down")) {
            if (sel < visible - 1) sel++;
        } else if (key_matches(&key, "enter")) {
            break;
        } else if (key_matches(&key, "escape") || key_matches(&key, "ctrl+c") ||
                   (buf[0] == 'q' && n == 1)) {
            sel = -1;
            break;
        }

        /* Move cursor up to redraw */
        char move[32];
        int mn = snprintf(move, sizeof(move), "\033[%dA", visible);
        write(STDOUT_FILENO, move, (size_t)mn);
    }

    /* Clear the rendered lines */
    for (int i = 0; i < visible; i++) {
        write(STDOUT_FILENO, "\033[2K\r\n", 6);
    }
    /* Move back up */
    char move[32];
    int mn = snprintf(move, sizeof(move), "\033[%dA", visible);
    write(STDOUT_FILENO, move, (size_t)mn);

    /* Show cursor */
    write(STDOUT_FILENO, "\033[?25h", 6);

    return sel;
}
