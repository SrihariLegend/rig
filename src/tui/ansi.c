#include "ansi.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void ansi_state_reset(AnsiState *state) {
    if (!state) return;
    memset(state, 0, sizeof(AnsiState));
    state->fg_color = -1;
    state->bg_color = -1;
}

void ansi_track(AnsiState *state, const char *seq) {
    if (!state || !seq) return;

    if (seq[0] != '\x1b' || seq[1] != '[') return;
    const char *p = seq + 2;

    int params[16] = {0};
    int param_count = 0;

    while (*p && *p != 'm') {
        if (isdigit((unsigned char)*p)) {
            int val = 0;
            while (isdigit((unsigned char)*p)) {
                val = val * 10 + (*p - '0');
                p++;
            }
            if (param_count < 16) params[param_count++] = val;
        }
        if (*p == ';') p++;
    }

    for (int i = 0; i < param_count; i++) {
        switch (params[i]) {
            case 0: ansi_state_reset(state); break;
            case 1: state->bold = true; break;
            case 2: state->dim = true; break;
            case 3: state->italic = true; break;
            case 4: state->underline = true; break;
            case 5: state->blink = true; break;
            case 7: state->inverse = true; break;
            case 8: state->hidden = true; break;
            case 9: state->strikethrough = true; break;
            case 22: state->bold = false; state->dim = false; break;
            case 23: state->italic = false; break;
            case 24: state->underline = false; break;
            case 25: state->blink = false; break;
            case 27: state->inverse = false; break;
            case 28: state->hidden = false; break;
            case 29: state->strikethrough = false; break;
            default:
                if (params[i] >= 30 && params[i] <= 37) state->fg_color = params[i] - 30;
                else if (params[i] >= 40 && params[i] <= 47) state->bg_color = params[i] - 40;
                else if (params[i] == 39) state->fg_color = -1;
                else if (params[i] == 49) state->bg_color = -1;
                else if (params[i] == 38 && i + 2 < param_count && params[i+1] == 5) {
                    state->fg_color = params[i+2];
                    i += 2;
                }
                else if (params[i] == 48 && i + 2 < param_count && params[i+1] == 5) {
                    state->bg_color = params[i+2];
                    i += 2;
                }
                break;
        }
    }
}

int ansi_strip_len(const char *str) {
    if (!str) return 0;
    int len = 0;
    const char *p = str;

    while (*p) {
        if (*p == '\x1b') {
            p++;
            if (*p == '[') {
                p++;
                while (*p && *p != 'm' && !isalpha((unsigned char)*p)) p++;
                if (*p) p++;
            }
            continue;
        }

        unsigned char c = (unsigned char)*p;
        int bytes = 1;
        if ((c & 0xE0) == 0xC0) bytes = 2;
        else if ((c & 0xF0) == 0xE0) bytes = 3;
        else if ((c & 0xF8) == 0xF0) bytes = 4;

        p += bytes;
        len++;
    }
    return len;
}

char *ansi_strip(const char *str) {
    if (!str) return NULL;

    int alloc = strlen(str) + 1;
    char *out = malloc(alloc);
    if (!out) return NULL;

    int pos = 0;
    const char *p = str;

    while (*p) {
        if (*p == '\x1b') {
            p++;
            if (*p == '[') {
                p++;
                while (*p && *p != 'm' && !isalpha((unsigned char)*p)) p++;
                if (*p) p++;
            }
            continue;
        }
        out[pos++] = *p++;
    }
    out[pos] = '\0';
    return out;
}

int unicode_char_width(unsigned int cp) {
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7f && cp < 0xa0)) return 0;

    if ((cp >= 0x1100 && cp <= 0x115f) ||
        cp == 0x2329 || cp == 0x232a ||
        (cp >= 0x2e80 && cp <= 0x303e) ||
        (cp >= 0x3040 && cp <= 0x33bf) ||
        (cp >= 0x3400 && cp <= 0x4dbf) ||
        (cp >= 0x4e00 && cp <= 0xa4cf) ||
        (cp >= 0xa960 && cp <= 0xa97c) ||
        (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe10 && cp <= 0xfe6f) ||
        (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6) ||
        (cp >= 0x1f000 && cp <= 0x1fbff) ||
        (cp >= 0x20000 && cp <= 0x2fffd) ||
        (cp >= 0x30000 && cp <= 0x3fffd)) {
        return 2;
    }

    return 1;
}

int unicode_display_width(const char *str) {
    if (!str) return 0;
    int width = 0;
    const unsigned char *p = (const unsigned char *)str;

    while (*p) {
        if (*p == '\x1b') {
            p++;
            if (*p == '[') {
                p++;
                while (*p && *p != 'm' && !isalpha(*p)) p++;
                if (*p) p++;
            }
            continue;
        }

        unsigned int cp = 0;
        int bytes = 1;

        if (*p < 0x80) {
            cp = *p;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            bytes = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            bytes = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            bytes = 4;
        }

        for (int i = 1; i < bytes && p[i]; i++) {
            cp = (cp << 6) | (p[i] & 0x3F);
        }

        width += unicode_char_width(cp);
        p += bytes;
    }
    return width;
}
