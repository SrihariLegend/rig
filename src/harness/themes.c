#include "themes.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/json.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Helper: duplicate a string
static char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

// Helper: lookup a variable by key
static const char *find_var(const Theme *t, const char *key) {
    if (!t || !key) return NULL;
    for (int i = 0; i < t->var_count; i++) {
        if (strcmp(t->vars[i].key, key) == 0) {
            return t->vars[i].value;
        }
    }
    return NULL;
}

// Helper: convert hex color (#RRGGBB) to ANSI escape code
static char *hex_to_ansi(const char *hex) {
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return strdup("");

    unsigned int r, g, b;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) return strdup("");

    char buf[32];
    snprintf(buf, sizeof(buf), "\033[38;2;%u;%u;%um", r, g, b);
    return strdup(buf);
}

// Helper: convert 256-color number to ANSI escape code
static char *num_to_ansi(const char *num) {
    if (!num || !isdigit(num[0])) return strdup("");

    int color = atoi(num);
    if (color < 0 || color > 255) return strdup("");

    char buf[32];
    snprintf(buf, sizeof(buf), "\033[38;5;%dm", color);
    return strdup(buf);
}

Theme *theme_load(const char *path) {
    if (!path) return NULL;

    cJSON *root = json_read_file(path);
    if (!root) return NULL;

    Theme *t = calloc(1, sizeof(Theme));
    if (!t) {
        cJSON_Delete(root);
        return NULL;
    }

    // Load name
    cJSON *name_obj = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (cJSON_IsString(name_obj)) {
        t->name = str_dup(name_obj->valuestring);
    } else {
        t->name = strdup("Unnamed");
    }

    // Load vars
    cJSON *vars_obj = cJSON_GetObjectItemCaseSensitive(root, "vars");
    if (cJSON_IsObject(vars_obj)) {
        cJSON *var = NULL;
        cJSON_ArrayForEach(var, vars_obj) {
            if (t->var_count >= THEME_MAX_VARS) break;
            if (!cJSON_IsString(var)) continue;

            t->vars[t->var_count].key = str_dup(var->string);
            t->vars[t->var_count].value = str_dup(var->valuestring);
            t->var_count++;
        }
    }

    // Load colors
    cJSON *colors_obj = cJSON_GetObjectItemCaseSensitive(root, "colors");
    if (cJSON_IsObject(colors_obj)) {
        cJSON *color = NULL;
        cJSON_ArrayForEach(color, colors_obj) {
            if (t->color_count >= THEME_MAX_COLORS) break;
            if (!cJSON_IsString(color)) continue;

            t->colors[t->color_count].token = str_dup(color->string);
            t->colors[t->color_count].color = str_dup(color->valuestring);
            t->color_count++;
        }
    }

    cJSON_Delete(root);
    return t;
}

Theme *theme_load_default(void) {
    Theme *t = calloc(1, sizeof(Theme));
    if (!t) return NULL;

    t->name = strdup("Dark");

    // Define variables
    const char *var_keys[] = {
        "primary", "secondary", "muted", "dim", "text",
        "success", "warning", "error", "info"
    };
    const char *var_vals[] = {
        "39", "242", "242", "240", "252",
        "34", "220", "196", "39"
    };

    for (int i = 0; i < 9 && i < THEME_MAX_VARS; i++) {
        t->vars[i].key = strdup(var_keys[i]);
        t->vars[i].value = strdup(var_vals[i]);
        t->var_count++;
    }

    // Define color tokens
    const char *token_keys[] = {
        "accent", "border", "success", "error", "muted", "dim", "text",
        "fg.default", "fg.muted", "fg.subtle", "fg.emphasis", "fg.success",
        "fg.warning", "fg.error", "fg.info", "fg.primary", "fg.secondary",
        "bg.default", "bg.subtle", "bg.emphasis", "bg.muted", "bg.overlay",
        "border.default", "border.muted", "border.emphasis", "border.subtle",
        "status.success", "status.warning", "status.error", "status.info",
        "link.default", "link.hover", "link.active", "link.visited",
        "panel.bg", "panel.border", "panel.title", "panel.subtitle",
        "input.bg", "input.border", "input.text", "input.placeholder",
        "button.bg", "button.text", "button.border", "button.hover",
        "badge.bg", "badge.text", "badge.border",
        "code.bg", "code.text", "code.border"
    };
    const char *token_vals[] = {
        "primary", "242", "success", "error", "muted", "dim", "text",
        "text", "muted", "dim", "text", "success",
        "warning", "error", "info", "primary", "secondary",
        "", "240", "242", "muted", "235",
        "242", "240", "text", "dim",
        "success", "warning", "error", "info",
        "primary", "39", "34", "242",
        "235", "242", "text", "muted",
        "235", "242", "text", "dim",
        "primary", "text", "primary", "39",
        "242", "text", "242",
        "235", "text", "242"
    };

    for (int i = 0; i < 51 && i < THEME_MAX_COLORS; i++) {
        t->colors[i].token = strdup(token_keys[i]);
        t->colors[i].color = strdup(token_vals[i]);
        t->color_count++;
    }

    return t;
}

char *theme_resolve_color(const Theme *t, const char *token) {
    if (!t || !token) return strdup("");

    // Find token in colors
    const char *color_value = NULL;
    for (int i = 0; i < t->color_count; i++) {
        if (strcmp(t->colors[i].token, token) == 0) {
            color_value = t->colors[i].color;
            break;
        }
    }

    if (!color_value || color_value[0] == '\0') return strdup("");

    // Check if it's a variable reference
    const char *var_value = find_var(t, color_value);
    if (var_value) {
        color_value = var_value;
    }

    // Convert to ANSI
    if (color_value[0] == '#') {
        return hex_to_ansi(color_value);
    } else if (isdigit(color_value[0])) {
        return num_to_ansi(color_value);
    } else {
        // Could be an unresolved variable or empty
        return strdup("");
    }
}

void theme_free(Theme *t) {
    if (!t) return;

    free(t->name);

    for (int i = 0; i < t->var_count; i++) {
        free(t->vars[i].key);
        free(t->vars[i].value);
    }

    for (int i = 0; i < t->color_count; i++) {
        free(t->colors[i].token);
        free(t->colors[i].color);
    }

    free(t);
}

// Context for readdir callback
typedef struct {
    char **paths;
    int count;
    int capacity;
} DiscoverCtx;

// Callback for fs_readdir
static void discover_callback(const char *dir, const char *name, bool is_dir, void *ctx) {
    if (is_dir) return;

    // Check if file ends with .json
    size_t len = strlen(name);
    if (len < 6 || strcmp(name + len - 5, ".json") != 0) return;

    DiscoverCtx *dctx = (DiscoverCtx *)ctx;

    // Expand capacity if needed
    if (dctx->count >= dctx->capacity) {
        int new_cap = dctx->capacity == 0 ? 16 : dctx->capacity * 2;
        char **new_paths = realloc(dctx->paths, sizeof(char *) * new_cap);
        if (!new_paths) return;
        dctx->paths = new_paths;
        dctx->capacity = new_cap;
    }

    // Add full path
    char *full_path = fs_join(dir, name);
    if (full_path) {
        dctx->paths[dctx->count++] = full_path;
    }
}

char **themes_discover(const char **paths, int path_count, int *out_count) {
    if (!paths || path_count <= 0 || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    DiscoverCtx ctx = {0};

    for (int i = 0; i < path_count; i++) {
        if (!paths[i]) continue;

        // Expand home directory if needed
        char *expanded = fs_expand_home(paths[i]);
        const char *path_to_scan = expanded ? expanded : paths[i];

        if (fs_is_dir(path_to_scan)) {
            fs_readdir(path_to_scan, discover_callback, &ctx);
        }

        free(expanded);
    }

    *out_count = ctx.count;

    // Add NULL terminator
    if (ctx.count > 0) {
        if (ctx.count >= ctx.capacity) {
            char **new_paths = realloc(ctx.paths, sizeof(char *) * (ctx.count + 1));
            if (!new_paths) {
                // Clean up on failure
                for (int i = 0; i < ctx.count; i++) {
                    free(ctx.paths[i]);
                }
                free(ctx.paths);
                *out_count = 0;
                return NULL;
            }
            ctx.paths = new_paths;
        }
        ctx.paths[ctx.count] = NULL;
    }

    return ctx.paths;
}
