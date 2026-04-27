#ifndef PI_HARNESS_PATH_SANDBOX_H
#define PI_HARNESS_PATH_SANDBOX_H

#include <stdbool.h>

typedef struct {
    char *project_root;       /* resolved absolute path */
    char **allowed_paths;     /* additional allowed paths (e.g., /tmp) */
    int allowed_count;
    int allowed_capacity;
    bool allow_home_config;   /* allow ~/.rig/ access */
} PathSandbox;

PathSandbox *sandbox_create(const char *project_root);
void sandbox_free(PathSandbox *sb);

/* Check if path is within sandbox */
bool sandbox_check(PathSandbox *sb, const char *path);

/* Add allowed path */
int sandbox_allow(PathSandbox *sb, const char *path);

/* Resolve and check -- returns resolved path or NULL if blocked */
char *sandbox_resolve(PathSandbox *sb, const char *path);

#endif
