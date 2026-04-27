#ifndef RIG_LANTERN_H
#define RIG_LANTERN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t r, g, b; } RGB;

typedef enum {
    FADE_LINEAR,
    FADE_EXPONENTIAL,
    FADE_SHARP,
} FadeCurve;

typedef enum {
    COLOR_TRUECOLOR,
    COLOR_256,
    COLOR_16,
    COLOR_NONE,
} ColorTier;

typedef struct {
    RGB accent;
    RGB warmth;
    RGB coolness;
    RGB error_color;
    FadeCurve curve;
    float depth;
    float floor;
    float warm_radius;
} LanternConfig;

typedef struct {
    LanternConfig config;
    ColorTier tier;
    RGB *lut;
    int lut_size;
    int term_height;
} Lantern;

LanternConfig lantern_defaults(void);
Lantern *lantern_create(const LanternConfig *config);
void lantern_free(Lantern *l);

void lantern_detect_color_tier(Lantern *l);
void lantern_rebuild_lut(Lantern *l, int term_height);

RGB lantern_fade_color(const Lantern *l, int distance_from_center);
RGB lantern_accent_at(const Lantern *l, int distance_from_center);
RGB lantern_error_at(const Lantern *l, int distance_from_center);

void lantern_emit_fg(const Lantern *l, const RGB *color, char *buf, int bufsize);
void lantern_emit_reset(char *buf, int bufsize);

RGB rgb_lerp(RGB a, RGB b, float t);

#endif
