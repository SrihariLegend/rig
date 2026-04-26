#ifndef PI_ANSI_H
#define PI_ANSI_H

#include <stdbool.h>

typedef struct {
    bool bold, dim, italic, underline, blink, inverse, hidden, strikethrough;
    int fg_color;
    int bg_color;
} AnsiState;

void ansi_state_reset(AnsiState *state);
void ansi_track(AnsiState *state, const char *sequence);
int ansi_strip_len(const char *str);
char *ansi_strip(const char *str);

int unicode_display_width(const char *str);
int unicode_char_width(unsigned int codepoint);

#endif
