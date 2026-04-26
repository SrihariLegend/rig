#include "prompts.h"
#include "util/fs.h"
#include "util/str.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>

// Parse frontmatter from markdown content
// Returns pointer to content after frontmatter, or original content if no frontmatter
static const char *parse_frontmatter(const char *content, char **description, char **argument_hint) {
    *description = NULL;
    *argument_hint = NULL;

    // Check for opening ---
    if (strncmp(content, "---\n", 4) != 0 && strncmp(content, "---\r\n", 5) != 0) {
        return content;
    }

    const char *fm_start = content + (strncmp(content, "---\n", 4) == 0 ? 4 : 5);

    // Find closing ---
    const char *end = strstr(fm_start, "\n---\n");
    if (!end) {
        end = strstr(fm_start, "\n---\r\n");
        if (!end) {
            return content; // No closing delimiter, treat as regular content
        }
    }

    // Parse frontmatter line by line
    const char *line_start = fm_start;
    while (line_start < end) {
        const char *line_end = strchr(line_start, '\n');
        if (!line_end || line_end > end) {
            line_end = end;
        }

        // Skip whitespace at start
        const char *key_start = line_start;
        while (key_start < line_end && isspace((unsigned char)*key_start)) {
            key_start++;
        }

        // Find colon
        const char *colon = key_start;
        while (colon < line_end && *colon != ':') {
            colon++;
        }

        if (colon < line_end) {
            // Extract key
            size_t key_len = (size_t)(colon - key_start);
            char key[256];
            if (key_len < sizeof(key)) {
                memcpy(key, key_start, key_len);
                key[key_len] = '\0';

                // Trim key
                char *key_p = key + key_len - 1;
                while (key_p >= key && isspace((unsigned char)*key_p)) {
                    *key_p = '\0';
                    key_p--;
                }

                // Extract value
                const char *value_start = colon + 1;
                while (value_start < line_end && isspace((unsigned char)*value_start)) {
                    value_start++;
                }

                const char *value_end = line_end;
                while (value_end > value_start && isspace((unsigned char)*(value_end - 1))) {
                    value_end--;
                }

                size_t value_len = (size_t)(value_end - value_start);
                if (value_len > 0) {
                    char *value = malloc(value_len + 1);
                    if (value) {
                        memcpy(value, value_start, value_len);
                        value[value_len] = '\0';

                        if (strcmp(key, "description") == 0) {
                            free(*description);
                            *description = value;
                        } else if (strcmp(key, "argument-hint") == 0 || strcmp(key, "argument_hint") == 0) {
                            free(*argument_hint);
                            *argument_hint = value;
                        } else {
                            free(value);
                        }
                    }
                }
            }
        }

        line_start = line_end;
        if (*line_start == '\n') line_start++;
    }

    // Return content after closing ---
    const char *body = end;
    while (*body && (*body == '\n' || *body == '\r' || *body == '-')) body++;
    return body;
}

// Callback for fs_readdir
typedef struct {
    PromptTemplate *templates;
    int count;
    int capacity;
    const char *dir_path;
} DiscoverContext;

static void discover_callback(const char *dir, const char *name, bool is_dir, void *ctx) {
    DiscoverContext *dc = (DiscoverContext *)ctx;

    if (is_dir) return;

    // Check if .md file
    size_t name_len = strlen(name);
    if (name_len < 4 || strcmp(name + name_len - 3, ".md") != 0) {
        return;
    }

    // Build full path
    char *full_path = fs_join(dir, name);
    if (!full_path) return;

    // Read file
    size_t content_len;
    char *content = fs_read_file(full_path, &content_len);
    if (!content) {
        free(full_path);
        return;
    }

    // Parse frontmatter
    char *description = NULL;
    char *argument_hint = NULL;
    const char *body = parse_frontmatter(content, &description, &argument_hint);

    // Extract name (filename without .md)
    char *template_name = malloc(name_len - 2);
    if (!template_name) {
        free(content);
        free(full_path);
        free(description);
        free(argument_hint);
        return;
    }
    memcpy(template_name, name, name_len - 3);
    template_name[name_len - 3] = '\0';

    // Check for duplicate name
    bool duplicate = false;
    for (int i = 0; i < dc->count; i++) {
        if (strcmp(dc->templates[i].name, template_name) == 0) {
            duplicate = true;
            break;
        }
    }

    if (duplicate) {
        free(template_name);
        free(content);
        free(full_path);
        free(description);
        free(argument_hint);
        return;
    }

    // Expand capacity if needed
    if (dc->count >= dc->capacity) {
        dc->capacity = dc->capacity ? dc->capacity * 2 : 8;
        dc->templates = realloc(dc->templates, (size_t)dc->capacity * sizeof(PromptTemplate));
    }

    // Copy template body
    char *body_copy = strdup(body);

    // Add template
    dc->templates[dc->count++] = (PromptTemplate){
        .name = template_name,
        .description = description,
        .argument_hint = argument_hint,
        .content = body_copy,
        .path = full_path
    };

    free(content);
}

PromptTemplate *prompts_discover(const char **paths, int path_count, int *out_count) {
    if (!paths || path_count <= 0 || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    DiscoverContext dc = {
        .templates = NULL,
        .count = 0,
        .capacity = 0,
        .dir_path = NULL
    };

    for (int i = 0; i < path_count; i++) {
        const char *path = paths[i];
        if (!path) continue;

        // Expand home directory if needed
        char *expanded = fs_expand_home(path);
        if (!expanded) continue;

        // Check if directory exists
        if (!fs_is_dir(expanded)) {
            free(expanded);
            continue;
        }

        dc.dir_path = expanded;
        fs_readdir(expanded, discover_callback, &dc);
        free(expanded);
    }

    *out_count = dc.count;
    return dc.templates;
}

// Parse ${@:N} or ${@:N:L} syntax
static bool parse_range_syntax(const char *p, int *start_idx, int *length) {
    // p points to character after ${@:
    *start_idx = 0;
    *length = -1; // -1 means "to end"

    // Parse N
    if (!isdigit((unsigned char)*p)) return false;

    *start_idx = 0;
    while (isdigit((unsigned char)*p)) {
        *start_idx = *start_idx * 10 + (*p - '0');
        p++;
    }

    if (*p == '}') {
        // Just ${@:N}
        return true;
    }

    if (*p != ':') return false;
    p++;

    // Parse L
    if (!isdigit((unsigned char)*p)) return false;

    *length = 0;
    while (isdigit((unsigned char)*p)) {
        *length = *length * 10 + (*p - '0');
        p++;
    }

    return *p == '}';
}

char *prompts_expand(const PromptTemplate *pt, const char **args, int arg_count) {
    if (!pt || !pt->content) return NULL;

    Str result = str_new(strlen(pt->content) * 2);
    const char *p = pt->content;

    while (*p) {
        if (*p == '$') {
            p++;

            // $1-$9 positional args
            if (*p >= '1' && *p <= '9') {
                int idx = *p - '1';
                if (idx < arg_count && args[idx]) {
                    str_append(&result, args[idx]);
                }
                p++;
            }
            // $@ or $ARGUMENTS - all args
            else if (*p == '@') {
                for (int i = 0; i < arg_count; i++) {
                    if (args[i]) {
                        if (i > 0) str_append_char(&result, ' ');
                        str_append(&result, args[i]);
                    }
                }
                p++;
            }
            else if (strncmp(p, "ARGUMENTS", 9) == 0) {
                for (int i = 0; i < arg_count; i++) {
                    if (args[i]) {
                        if (i > 0) str_append_char(&result, ' ');
                        str_append(&result, args[i]);
                    }
                }
                p += 9;
            }
            // ${@:N} or ${@:N:L}
            else if (*p == '{' && p[1] == '@' && p[2] == ':') {
                p += 3; // Skip ${@:

                int start_idx, length;
                const char *scan = p;
                if (parse_range_syntax(scan, &start_idx, &length)) {
                    // Convert 1-indexed to 0-indexed
                    int start = start_idx - 1;
                    if (start < 0) start = 0;

                    int end = length >= 0 ? start + length : arg_count;
                    if (end > arg_count) end = arg_count;

                    for (int i = start; i < end; i++) {
                        if (args[i]) {
                            if (i > start) str_append_char(&result, ' ');
                            str_append(&result, args[i]);
                        }
                    }

                    // Skip to }
                    while (*p && *p != '}') p++;
                    if (*p == '}') p++;
                } else {
                    // Invalid syntax, output literally
                    str_append(&result, "${@:");
                }
            }
            else {
                // Not a recognized pattern, output $ literally
                str_append_char(&result, '$');
            }
        } else {
            str_append_char(&result, *p);
            p++;
        }
    }

    return str_take(&result);
}

void prompts_free(PromptTemplate *templates, int count) {
    if (!templates) return;

    for (int i = 0; i < count; i++) {
        free(templates[i].name);
        free(templates[i].description);
        free(templates[i].argument_hint);
        free(templates[i].content);
        free(templates[i].path);
    }

    free(templates);
}
