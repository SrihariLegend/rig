#ifndef PI_TERMINAL_H
#define PI_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>

void terminal_enter_raw_mode(void);
void terminal_exit_raw_mode(void);
bool terminal_is_raw(void);

void terminal_get_size(int *cols, int *rows);
void terminal_hide_cursor(void);
void terminal_show_cursor(void);
void terminal_move_cursor(int row, int col);
void terminal_clear_line(void);
void terminal_clear_screen(void);
void terminal_write(const char *data, size_t len);
void terminal_write_str(const char *str);
void terminal_flush(void);

void terminal_enable_kitty_keyboard(void);
void terminal_disable_kitty_keyboard(void);
void terminal_enable_bracketed_paste(void);
void terminal_disable_bracketed_paste(void);
void terminal_enable_mouse(void);
void terminal_disable_mouse(void);

void terminal_sync_begin(void);
void terminal_sync_end(void);

void terminal_enter_alt_screen(void);
void terminal_exit_alt_screen(void);

#endif
