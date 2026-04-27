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
        if (arg_summary && fnmatch(r->pattern, arg_summary, FNM_NOESCAPE) == 0) return true;
    }

    return false;
}

char *permissions_describe_call(const char *tool, const char *arg_summary) {
    Str desc = str_new(256);
    str_appendf(&desc, "%s", tool);
    if (arg_summary && arg_summary[0]) {
        int len = (int)strlen(arg_summary);
        if (len > 120) {
            /* Walk back to UTF-8 char boundary */
            int cut = 117;
            while (cut > 0 && ((unsigned char)arg_summary[cut] & 0xC0) == 0x80) cut--;
            str_append_len(&desc, ": ", 2);
            str_append_len(&desc, arg_summary, cut);
            str_append(&desc, "...");
        } else {
            str_appendf(&desc, ": %s", arg_summary);
        }
    }
    return str_take(&desc);
}
