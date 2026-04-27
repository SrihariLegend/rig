#include "config.h"
#include "util/fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

static char agent_dir[PATH_MAX] = {0};
static char global_settings[PATH_MAX] = {0};
static char project_settings[PATH_MAX] = {0};
static char auth_path[PATH_MAX] = {0};
static char models_path_buf[PATH_MAX] = {0};
static char sessions_dir[PATH_MAX] = {0};

const char *config_agent_dir(void) {
    if (!agent_dir[0]) {
        char *home = fs_expand_home("~/.rig/agent");
        if (home) {
            strncpy(agent_dir, home, PATH_MAX - 1);
            free(home);
        }
    }
    return agent_dir;
}

const char *config_settings_global_path(void) {
    if (!global_settings[0]) {
        snprintf(global_settings, PATH_MAX, "%s/settings.json", config_agent_dir());
    }
    return global_settings;
}

const char *config_settings_project_path(void) {
    if (!project_settings[0]) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(project_settings, PATH_MAX, "%s/.rig/settings.json", cwd);
        }
    }
    return project_settings;
}

const char *config_auth_path(void) {
    if (!auth_path[0]) {
        snprintf(auth_path, PATH_MAX, "%s/auth.json", config_agent_dir());
    }
    return auth_path;
}

const char *config_models_path(void) {
    if (!models_path_buf[0]) {
        snprintf(models_path_buf, PATH_MAX, "%s/models.json", config_agent_dir());
    }
    return models_path_buf;
}

const char *config_sessions_dir(void) {
    if (!sessions_dir[0]) {
        snprintf(sessions_dir, PATH_MAX, "%s/sessions", config_agent_dir());
    }
    return sessions_dir;
}

char *config_find_project_root(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return strdup(".");

    char path[PATH_MAX];
    strncpy(path, cwd, PATH_MAX - 1);

    while (strlen(path) > 1) {
        char git[PATH_MAX];
        snprintf(git, PATH_MAX, "%s/.git", path);
        if (fs_exists(git)) return strdup(path);

        char *slash = strrchr(path, '/');
        if (slash && slash != path) *slash = '\0';
        else break;
    }
    return strdup(cwd);
}
