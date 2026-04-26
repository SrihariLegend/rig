#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static struct termios orig_termios;
static bool raw_mode = false;

void terminal_enter_raw_mode(void) {
    if (raw_mode) return;
    if (!isatty(STDIN_FILENO)) return;

    tcgetattr(STDIN_FILENO, &orig_termios);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned long)(OPOST);
    raw.c_cflag |= (unsigned long)(CS8);
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode = true;
}

void terminal_exit_raw_mode(void) {
    if (!raw_mode) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode = false;
}

bool terminal_is_raw(void) {
    return raw_mode;
}

void terminal_get_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (cols) *cols = ws.ws_col;
        if (rows) *rows = ws.ws_row;
    } else {
        if (cols) *cols = 80;
        if (rows) *rows = 24;
    }
}

void terminal_hide_cursor(void) {
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

void terminal_show_cursor(void) {
    write(STDOUT_FILENO, "\x1b[?25h", 6);
}

void terminal_move_cursor(int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    write(STDOUT_FILENO, buf, n);
}

void terminal_clear_line(void) {
    write(STDOUT_FILENO, "\x1b[2K", 4);
}

void terminal_clear_screen(void) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

void terminal_write(const char *data, size_t len) {
    if (data && len > 0) write(STDOUT_FILENO, data, len);
}

void terminal_write_str(const char *str) {
    if (str) write(STDOUT_FILENO, str, strlen(str));
}

void terminal_flush(void) {
    fsync(STDOUT_FILENO);
}

void terminal_enable_kitty_keyboard(void) {
    write(STDOUT_FILENO, "\x1b[>1u", 5);
}

void terminal_disable_kitty_keyboard(void) {
    write(STDOUT_FILENO, "\x1b[<u", 4);
}

void terminal_enable_bracketed_paste(void) {
    write(STDOUT_FILENO, "\x1b[?2004h", 8);
}

void terminal_disable_bracketed_paste(void) {
    write(STDOUT_FILENO, "\x1b[?2004l", 8);
}

void terminal_enable_mouse(void) {
    write(STDOUT_FILENO, "\x1b[?1006h\x1b[?1003h", 16);
}

void terminal_disable_mouse(void) {
    write(STDOUT_FILENO, "\x1b[?1003l\x1b[?1006l", 16);
}

void terminal_sync_begin(void) {
    write(STDOUT_FILENO, "\x1b[?2026h", 8);
}

void terminal_sync_end(void) {
    write(STDOUT_FILENO, "\x1b[?2026l", 8);
}
