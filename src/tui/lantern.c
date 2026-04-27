#include "lantern.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

LanternConfig lantern_defaults(void) {
    return (LanternConfig){
        .accent    = { 212, 160, 84 },
        .warmth    = { 210, 214, 220 },
        .coolness  = { 90, 100, 115 },
        .error_color = { 180, 100, 80 },
        .curve     = FADE_EXPONENTIAL,
        .depth     = 0.75f,
        .floor     = 0.08f,
        .warm_radius = 0.35f,
    };
}

Lantern *lantern_create(const LanternConfig *config) {
    Lantern *l = calloc(1, sizeof(Lantern));
    if (!l) return NULL;
    l->config = config ? *config : lantern_defaults();
    l->tier = COLOR_TRUECOLOR;
    return l;
}

void lantern_free(Lantern *l) {
    if (!l) return;
    free(l->lut);
    free(l);
}

void lantern_detect_color_tier(Lantern *l) {
    const char *no_color = getenv("NO_COLOR");
    if (no_color && no_color[0]) {
        l->tier = COLOR_NONE;
        return;
    }

    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 ||
                      strcmp(colorterm, "24bit") == 0)) {
        l->tier = COLOR_TRUECOLOR;
        return;
    }

    const char *term_program = getenv("TERM_PROGRAM");
    if (term_program && (strcmp(term_program, "iTerm.app") == 0 ||
                         strcmp(term_program, "WezTerm") == 0 ||
                         strcmp(term_program, "Hyper") == 0)) {
        l->tier = COLOR_TRUECOLOR;
        return;
    }

    const char *term = getenv("TERM");
    if (term && strstr(term, "256color")) {
        l->tier = COLOR_256;
        return;
    }

    l->tier = COLOR_16;
}

RGB rgb_lerp(RGB a, RGB b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return (RGB){
        .r = (uint8_t)(a.r + (int)((b.r - a.r) * t)),
        .g = (uint8_t)(a.g + (int)((b.g - a.g) * t)),
        .b = (uint8_t)(a.b + (int)((b.b - a.b) * t)),
    };
}

void lantern_rebuild_lut(Lantern *l, int term_height) {
    if (term_height <= 0) term_height = 24;
    l->term_height = term_height;

    int lut_size = term_height;
    RGB *lut = realloc(l->lut, (size_t)lut_size * sizeof(RGB));
    if (!lut) return;
    l->lut = lut;
    l->lut_size = lut_size;

    int warm_radius = (int)(term_height * l->config.warm_radius);
    int fade_depth = (int)(term_height * l->config.depth);
    if (fade_depth < 1) fade_depth = 1;

    float floor = l->config.floor;

    for (int d = 0; d < lut_size; d++) {
        float t;
        if (d <= warm_radius) {
            t = 0.0f;
        } else {
            t = (float)(d - warm_radius) / (float)fade_depth;
            if (t > 1.0f) t = 1.0f;

            switch (l->config.curve) {
            case FADE_EXPONENTIAL: t = t * t; break;
            case FADE_SHARP:       t = t * t * t; break;
            case FADE_LINEAR:      break;
            }
        }

        float brightness = 1.0f - t * (1.0f - floor);
        RGB base = rgb_lerp(l->config.warmth, l->config.coolness, t);
        lut[d] = (RGB){
            .r = (uint8_t)(base.r * brightness),
            .g = (uint8_t)(base.g * brightness),
            .b = (uint8_t)(base.b * brightness),
        };
    }
}

RGB lantern_fade_color(const Lantern *l, int distance) {
    if (!l->lut || l->lut_size == 0)
        return l->config.warmth;
    if (distance < 0) distance = -distance;
    if (distance >= l->lut_size) distance = l->lut_size - 1;
    return l->lut[distance];
}

static float fade_brightness(const Lantern *l, int distance) {
    RGB base = lantern_fade_color(l, distance);
    float base_sum = (float)base.r + base.g + base.b;
    float warmth_sum = (float)l->config.warmth.r + l->config.warmth.g + l->config.warmth.b;
    if (warmth_sum < 1.0f) warmth_sum = 1.0f;
    float b = base_sum / warmth_sum;
    return b > 1.0f ? 1.0f : b;
}

RGB lantern_accent_at(const Lantern *l, int distance) {
    float b = fade_brightness(l, distance);
    return (RGB){
        .r = (uint8_t)(l->config.accent.r * b),
        .g = (uint8_t)(l->config.accent.g * b),
        .b = (uint8_t)(l->config.accent.b * b),
    };
}

RGB lantern_error_at(const Lantern *l, int distance) {
    float b = fade_brightness(l, distance);
    return (RGB){
        .r = (uint8_t)(l->config.error_color.r * b),
        .g = (uint8_t)(l->config.error_color.g * b),
        .b = (uint8_t)(l->config.error_color.b * b),
    };
}

void lantern_emit_fg(const Lantern *l, const RGB *color, char *buf, int bufsize) {
    switch (l->tier) {
    case COLOR_TRUECOLOR:
        snprintf(buf, bufsize, "\x1b[38;2;%d;%d;%dm", color->r, color->g, color->b);
        break;
    case COLOR_256: {
        int idx;
        if (color->r == color->g && color->g == color->b) {
            int gray = (color->r - 8) / 10;
            if (gray < 0) gray = 0;
            if (gray > 23) gray = 23;
            idx = 232 + gray;
        } else {
            int r6 = (color->r * 5 + 127) / 255;
            int g6 = (color->g * 5 + 127) / 255;
            int b6 = (color->b * 5 + 127) / 255;
            idx = 16 + 36 * r6 + 6 * g6 + b6;
        }
        snprintf(buf, bufsize, "\x1b[38;5;%dm", idx);
        break;
    }
    case COLOR_16: {
        int avg = (color->r + color->g + color->b) / 3;
        if (avg > 180)      snprintf(buf, bufsize, "\x1b[1;37m");
        else if (avg > 120)  snprintf(buf, bufsize, "\x1b[37m");
        else if (avg > 60)   snprintf(buf, bufsize, "\x1b[2;37m");
        else                  snprintf(buf, bufsize, "\x1b[2;90m");
        break;
    }
    case COLOR_NONE:
        buf[0] = '\0';
        break;
    }
}

void lantern_emit_reset(char *buf, int bufsize) {
    snprintf(buf, bufsize, "\x1b[0m");
}
