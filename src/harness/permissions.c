#include "permissions.h"
#include "util/str.h"
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

PermissionSet *permissions_create(void) {
    PermissionSet *ps = calloc(1, sizeof(PermissionSet));
    if (!ps) return NULL;
    ps->capacity = 16;
    ps->rules = calloc(ps->capacity, sizeof(TrustRule));
    return ps;
}

void permissions_free(PermissionSet *ps) {
    if (!ps) return;
    for (int i = 0; i < ps->count; i++) {
        free(ps->rules[i].tool);
        free(ps->rules[i].pattern);
    }
    free(ps->rules);
    free(ps);
}

void permissions_trust(PermissionSet *ps, const char *tool, const char *pattern) {
    if (!ps || !tool) return;

    /* Check for yolo mode */
    if (strcmp(tool, "*") == 0 && !pattern) {
        ps->yolo = true;
        return;
    }

    /* Check for duplicate */
    for (int i = 0; i < ps->count; i++) {
        if (strcmp(ps->rules[i].tool, tool) == 0) {
            bool same_pattern = (!ps->rules[i].pattern && !pattern) ||
                (ps->rules[i].pattern && pattern && strcmp(ps->rules[i].pattern, pattern) == 0);
            if (same_pattern) return;
        }
    }

    if (ps->count >= ps->capacity) {
        int new_cap = ps->capacity * 2;
        TrustRule *nr = realloc(ps->rules, (size_t)new_cap * sizeof(TrustRule));
        if (!nr) return;
        ps->rules = nr;
        ps->capacity = new_cap;
    }

    ps->rules[ps->count++] = (TrustRule){
        .tool = strdup(tool),
        .pattern = pattern ? strdup(pattern) : NULL,
    };
}

bool permissions_check(const PermissionSet *ps, const char *tool, const char *arg_summary) {
    if (!ps || !tool) return false;
    if (ps->yolo) return true;

    for (int i = 0; i < ps->count; i++) {
        TrustRule *r = &ps->rules[i];

        /* Tool match: exact or wildcard */
        bool tool_match = (strcmp(r->tool, "*") == 0) || (strcmp(r->tool, tool) == 0);
        if (!tool_match) continue;

        /* Pattern match: NULL = any, otherwise glob */
        if (!r->pattern) return true;
        if (arg_summary && fnmatch(r->pattern, arg_summary, 0) == 0) return true;
    }

    return false;
}

char *permissions_describe_call(const char *tool, const char *arg_summary) {
    Str desc = str_new(256);
    str_appendf(&desc, "%s", tool);
    if (arg_summary && arg_summary[0]) {
        /* Truncate long args */
        int len = (int)strlen(arg_summary);
        if (len > 120) {
            char truncated[128];
            memcpy(truncated, arg_summary, 117);
            truncated[117] = '.';
            truncated[118] = '.';
            truncated[119] = '.';
            truncated[120] = '\0';
            str_appendf(&desc, ": %s", truncated);
        } else {
            str_appendf(&desc, ": %s", arg_summary);
        }
    }
    return str_take(&desc);
}
