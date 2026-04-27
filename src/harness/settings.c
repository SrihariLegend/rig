#include "settings.h"
#include "util/json.h"
#include "util/fs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Built-in defaults                                                  */
/* ------------------------------------------------------------------ */

static cJSON *settings_make_defaults(void) {
    cJSON *d = cJSON_CreateObject();
    if (!d) return NULL;

    cJSON_AddStringToObject(d, "default_thinking_level", "medium");
    cJSON_AddStringToObject(d, "theme", "default");

    cJSON *compaction = cJSON_AddObjectToObject(d, "compaction");
    if (compaction) {
        cJSON_AddBoolToObject(compaction, "enabled", 1);
    }

    cJSON *retry = cJSON_AddObjectToObject(d, "retry");
    if (retry) {
        cJSON_AddBoolToObject(retry, "enabled", 1);
        cJSON_AddNumberToObject(retry, "max_retries", 3);
    }

    cJSON *terminal = cJSON_AddObjectToObject(d, "terminal");
    if (terminal) {
        cJSON_AddBoolToObject(terminal, "show_images", 1);
    }

    cJSON *snapshots = cJSON_AddObjectToObject(d, "snapshots");
    if (snapshots) {
        cJSON_AddNumberToObject(snapshots, "max_mb", 64);
        cJSON_AddNumberToObject(snapshots, "file_max_mb", 1);
    }

    return d;
}

/* ------------------------------------------------------------------ */
/*  Internal: recompute the merged layer                               */
/* ------------------------------------------------------------------ */

static void settings_recompute(SettingsManager *sm) {
    if (sm->merged) {
        cJSON_Delete(sm->merged);
        sm->merged = NULL;
    }

    /* Start from defaults */
    cJSON *defaults = settings_make_defaults();

    /* defaults < global */
    cJSON *step1 = json_deep_merge(defaults, sm->global);
    cJSON_Delete(defaults);

    /* ... < project */
    cJSON *step2 = json_deep_merge(step1, sm->project);
    cJSON_Delete(step1);

    /* ... < cli */
    cJSON *step3 = json_deep_merge(step2, sm->cli);
    cJSON_Delete(step2);

    /* Remove any keys that are cJSON_NULL (higher-layer delete) */
    sm->merged = step3;
}

/* Helper: remove cJSON_NULL entries recursively from an object */
static void prune_nulls(cJSON *obj) {
    if (!obj || !cJSON_IsObject(obj)) return;

    cJSON *child = obj->child;
    while (child) {
        cJSON *next = child->next;
        if (cJSON_IsNull(child)) {
            cJSON_DetachItemViaPointer(obj, child);
            cJSON_Delete(child);
        } else if (cJSON_IsObject(child)) {
            prune_nulls(child);
        }
        child = next;
    }
}

/* Full recompute with null-pruning */
static void settings_recompute_full(SettingsManager *sm) {
    settings_recompute(sm);
    if (sm->merged) {
        prune_nulls(sm->merged);
    }
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

SettingsManager *settings_create(const char *global_path, const char *project_path) {
    SettingsManager *sm = calloc(1, sizeof(SettingsManager));
    if (!sm) return NULL;

    sm->global_path  = global_path  ? strdup(global_path)  : NULL;
    sm->project_path = project_path ? strdup(project_path) : NULL;

    /* Try to load files; fall back to empty objects */
    if (global_path && fs_is_file(global_path)) {
        sm->global = json_read_file(global_path);
    }
    if (!sm->global) {
        sm->global = cJSON_CreateObject();
    }

    if (project_path && fs_is_file(project_path)) {
        sm->project = json_read_file(project_path);
    }
    if (!sm->project) {
        sm->project = cJSON_CreateObject();
    }

    sm->cli = cJSON_CreateObject();

    settings_recompute_full(sm);
    return sm;
}

void settings_free(SettingsManager *sm) {
    if (!sm) return;

    cJSON_Delete(sm->global);
    cJSON_Delete(sm->project);
    cJSON_Delete(sm->cli);
    cJSON_Delete(sm->merged);

    free(sm->global_path);
    free(sm->project_path);
    free(sm);
}

/* ------------------------------------------------------------------ */
/*  Get / Set                                                          */
/* ------------------------------------------------------------------ */

cJSON *settings_get(SettingsManager *sm, const char *key) {
    if (!sm || !key) return NULL;
    return json_get(sm->merged, key);
}

static cJSON **layer_ptr(SettingsManager *sm, SettingsLayer layer) {
    switch (layer) {
        case SETTINGS_GLOBAL:  return &sm->global;
        case SETTINGS_PROJECT: return &sm->project;
        case SETTINGS_CLI:     return &sm->cli;
    }
    return NULL;
}

/* Set a value at a dot-path in the given layer.
 * The caller transfers ownership of `value` to the settings manager. */
int settings_set(SettingsManager *sm, SettingsLayer layer, const char *key, cJSON *value) {
    if (!sm || !key) return -1;

    cJSON **lp = layer_ptr(sm, layer);
    if (!lp || !*lp) return -1;

    cJSON *root = *lp;

    /* Use json.h helpers to navigate/create the dot-path. */
    /* We need to split the final key ourselves because json_set_* are typed. */
    /* Walk dot-segments, creating intermediate objects. */

    const char *p = key;
    cJSON *current = root;

    /* Find last dot to split parent path and final key */
    const char *last_dot = strrchr(key, '.');
    if (last_dot) {
        /* Create intermediate objects */
        size_t parent_len = (size_t)(last_dot - key);
        char *parent_path = malloc(parent_len + 1);
        if (!parent_path) return -1;
        memcpy(parent_path, key, parent_len);
        parent_path[parent_len] = '\0';

        /* Walk and create each segment */
        char *seg = parent_path;
        while (seg && *seg) {
            char *dot = strchr(seg, '.');
            if (dot) *dot = '\0';

            cJSON *child = cJSON_GetObjectItemCaseSensitive(current, seg);
            if (!child) {
                child = cJSON_CreateObject();
                cJSON_AddItemToObject(current, seg, child);
            } else if (!cJSON_IsObject(child)) {
                /* Overwrite non-object with object */
                cJSON_DeleteItemFromObject(current, seg);
                child = cJSON_CreateObject();
                cJSON_AddItemToObject(current, seg, child);
            }
            current = child;

            seg = dot ? dot + 1 : NULL;
        }
        free(parent_path);
        p = last_dot + 1;
    } else {
        p = key;
    }

    /* Remove existing item at final key */
    cJSON_DeleteItemFromObject(current, p);

    /* Add the new value (or NULL sentinel for deletion) */
    if (value) {
        /* Ensure the item has the correct key name */
        if (value->string) {
            free(value->string);
            value->string = NULL;
        }
        cJSON_AddItemToObject(current, p, value);
    } else {
        /* NULL means "delete this key in the merge" - store cJSON_NULL */
        cJSON *null_val = cJSON_CreateNull();
        cJSON_AddItemToObject(current, p, null_val);
    }

    settings_recompute_full(sm);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Flush / Reload                                                     */
/* ------------------------------------------------------------------ */

int settings_flush(SettingsManager *sm) {
    if (!sm) return -1;

    int rc = 0;

    if (sm->global_path && sm->global) {
        /* Ensure parent directory exists */
        char *dir = strdup(sm->global_path);
        if (dir) {
            char *slash = strrchr(dir, '/');
            if (slash) {
                *slash = '\0';
                fs_mkdir_p(dir);
            }
            free(dir);
        }
        if (json_write_file(sm->global_path, sm->global) != 0) rc = -1;
    }

    if (sm->project_path && sm->project) {
        char *dir = strdup(sm->project_path);
        if (dir) {
            char *slash = strrchr(dir, '/');
            if (slash) {
                *slash = '\0';
                fs_mkdir_p(dir);
            }
            free(dir);
        }
        if (json_write_file(sm->project_path, sm->project) != 0) rc = -1;
    }

    return rc;
}

int settings_reload(SettingsManager *sm) {
    if (!sm) return -1;

    /* Reload global */
    if (sm->global_path && fs_is_file(sm->global_path)) {
        cJSON *g = json_read_file(sm->global_path);
        if (g) {
            cJSON_Delete(sm->global);
            sm->global = g;
        }
    }

    /* Reload project */
    if (sm->project_path && fs_is_file(sm->project_path)) {
        cJSON *p = json_read_file(sm->project_path);
        if (p) {
            cJSON_Delete(sm->project);
            sm->project = p;
        }
    }

    settings_recompute_full(sm);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Typed getters                                                      */
/* ------------------------------------------------------------------ */

const char *settings_get_string(SettingsManager *sm, const char *key, const char *def) {
    if (!sm || !key) return def;
    cJSON *item = json_get(sm->merged, key);
    if (!item || !cJSON_IsString(item)) return def;
    return item->valuestring;
}

int settings_get_int(SettingsManager *sm, const char *key, int def) {
    if (!sm || !key) return def;
    cJSON *item = json_get(sm->merged, key);
    if (!item || !cJSON_IsNumber(item)) return def;
    return item->valueint;
}

bool settings_get_bool(SettingsManager *sm, const char *key, bool def) {
    if (!sm || !key) return def;
    cJSON *item = json_get(sm->merged, key);
    if (!item || !cJSON_IsBool(item)) return def;
    return cJSON_IsTrue(item);
}

double settings_get_double(SettingsManager *sm, const char *key, double def) {
    if (!sm || !key) return def;
    cJSON *item = json_get(sm->merged, key);
    if (!item || !cJSON_IsNumber(item)) return def;
    return item->valuedouble;
}
