#ifndef PI_HARNESS_THEMES_H
#define PI_HARNESS_THEMES_H

#define THEME_MAX_VARS 32
#define THEME_MAX_COLORS 64

typedef struct {
    char *name;
    struct { char *key; char *value; } vars[THEME_MAX_VARS];
    int var_count;
    struct { char *token; char *color; } colors[THEME_MAX_COLORS];
    int color_count;
} Theme;

// Load theme from JSON file
Theme *theme_load(const char *path);

// Load built-in dark theme (default)
Theme *theme_load_default(void);

// Resolve a color token to an ANSI color code string
// Returns malloc'd string like "\033[38;5;42m" or "\033[38;2;255;0;0m"
// Returns empty string for default/unresolved
char *theme_resolve_color(const Theme *t, const char *token);

// Free theme
void theme_free(Theme *t);

// Discover themes from directories (scan for .json files)
char **themes_discover(const char **paths, int path_count, int *out_count);

#endif
