/* path_sandbox.c — restrict file access to project root and allowed paths */
#include "harness/path_sandbox.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <pwd.h>

#define INITIAL_ALLOWED 8

PathSandbox *sandbox_create(const char *project_root) {
    if (!project_root) return NULL;

    PathSandbox *sb = calloc(1, sizeof(PathSandbox));
    if (!sb) return NULL;

    char resolved[PATH_MAX];
    if (!realpath(project_root, resolved)) {
        free(sb);
        return NULL;
    }
    sb->project_root = strdup(resolved);

    sb->allowed_paths = calloc(INITIAL_ALLOWED, sizeof(char *));
    if (!sb->allowed_paths) {
        free(sb->project_root);
        free(sb);
        return NULL;
    }
    sb->allowed_capacity = INITIAL_ALLOWED;
    sb->allowed_count = 0;
    sb->allow_home_config = true;

    return sb;
}

void sandbox_free(PathSandbox *sb) {
    if (!sb) return;
    free(sb->project_root);
    for (int i = 0; i < sb->allowed_count; i++) {
        free(sb->allowed_paths[i]);
    }
    free(sb->allowed_paths);
    free(sb);
}

int sandbox_allow(PathSandbox *sb, const char *path) {
    if (!sb || !path) return -1;

    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) return -1;

    /* Check for duplicates */
    for (int i = 0; i < sb->allowed_count; i++) {
        if (strcmp(sb->allowed_paths[i], resolved) == 0) return 0;
    }

    /* Grow if needed */
    if (sb->allowed_count >= sb->allowed_capacity) {
        int new_cap = sb->allowed_capacity * 2;
        char **new_paths = realloc(sb->allowed_paths, (size_t)new_cap * sizeof(char *));
        if (!new_paths) return -1;
        sb->allowed_paths = new_paths;
        sb->allowed_capacity = new_cap;
    }

    sb->allowed_paths[sb->allowed_count] = strdup(resolved);
    sb->allowed_count++;
    return 0;
}

/* Check if target starts with prefix as a directory path */
static bool path_starts_with(const char *target, const char *prefix) {
    size_t plen = strlen(prefix);
    if (strncmp(target, prefix, plen) != 0) return false;
    /* Must be exact match or followed by '/' */
    return target[plen] == '\0' || target[plen] == '/';
}

static char *get_home_config_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) return NULL;

    size_t len = strlen(home) + 6; /* "/.rig" + null */
    char *path = malloc(len);
    if (!path) return NULL;
    snprintf(path, len, "%s/.rig", home);
    return path;
}

bool sandbox_check(PathSandbox *sb, const char *path) {
    if (!sb || !path) return false;

    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        /* Path doesn't exist yet — try resolving the parent directory */
        char *path_copy = strdup(path);
        if (!path_copy) return false;

        /* Find last '/' */
        char *last_slash = strrchr(path_copy, '/');
        if (!last_slash) {
            free(path_copy);
            return false;
        }

        /* Resolve the parent */
        if (last_slash == path_copy) {
            /* Root directory */
            free(path_copy);
            return false;
        }
        *last_slash = '\0';
        char parent_resolved[PATH_MAX];
        if (!realpath(path_copy, parent_resolved)) {
            free(path_copy);
            return false;
        }

        /* Reconstruct: parent_resolved + "/" + filename */
        snprintf(resolved, PATH_MAX, "%s/%s", parent_resolved, last_slash + 1);
        free(path_copy);
    }

    /* Check project root */
    if (path_starts_with(resolved, sb->project_root)) return true;

    /* Check allowed paths */
    for (int i = 0; i < sb->allowed_count; i++) {
        if (path_starts_with(resolved, sb->allowed_paths[i])) return true;
    }

    /* Check home config */
    if (sb->allow_home_config) {
        char *config_path = get_home_config_path();
        if (config_path) {
            char config_resolved[PATH_MAX];
            bool allowed = false;
            if (realpath(config_path, config_resolved)) {
                allowed = path_starts_with(resolved, config_resolved);
            }
            free(config_path);
            if (allowed) return true;
        }
    }

    return false;
}

char *sandbox_resolve(PathSandbox *sb, const char *path) {
    if (!sb || !path) return NULL;

    if (!sandbox_check(sb, path)) return NULL;

    char resolved[PATH_MAX];
    if (realpath(path, resolved)) {
        return strdup(resolved);
    }

    /* For non-existent paths that passed sandbox_check, reconstruct */
    char *path_copy = strdup(path);
    if (!path_copy) return NULL;

    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        free(path_copy);
        return NULL;
    }

    if (last_slash == path_copy) {
        free(path_copy);
        return NULL;
    }

    *last_slash = '\0';
    char parent_resolved[PATH_MAX];
    if (!realpath(path_copy, parent_resolved)) {
        free(path_copy);
        return NULL;
    }

    char *result = malloc(PATH_MAX);
    if (!result) {
        free(path_copy);
        return NULL;
    }
    snprintf(result, PATH_MAX, "%s/%s", parent_resolved, last_slash + 1);
    free(path_copy);
    return result;
}
