#include "packages.h"
#include "config.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/process.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <dirent.h>

// Parse a package specifier into type and clean name
PackageSourceType package_parse_specifier(const char *specifier, char **clean_name) {
    if (!specifier) {
        if (clean_name) *clean_name = NULL;
        return PKG_LOCAL;
    }

    // npm:package-name or npm:@scope/package@version
    if (strncmp(specifier, "npm:", 4) == 0) {
        if (clean_name) *clean_name = strdup(specifier + 4);
        return PKG_NPM;
    }

    // git:url or https://github.com/...
    if (strncmp(specifier, "git:", 4) == 0) {
        if (clean_name) *clean_name = strdup(specifier + 4);
        return PKG_GIT;
    }
    if (strncmp(specifier, "https://", 8) == 0 || strncmp(specifier, "http://", 7) == 0) {
        if (clean_name) *clean_name = strdup(specifier);
        return PKG_GIT;
    }

    // Local path (absolute or relative)
    if (clean_name) *clean_name = strdup(specifier);
    return PKG_LOCAL;
}

// Get the install directory for a package source type
char *package_install_dir(PackageSourceType type, bool local) {
    char buf[PATH_MAX];

    if (local) {
        // Project-local: .rig/npm/ or .rig/git/
        char *root = config_find_project_root();
        if (type == PKG_NPM) {
            snprintf(buf, PATH_MAX, "%s/.rig/npm", root);
        } else if (type == PKG_GIT) {
            snprintf(buf, PATH_MAX, "%s/.rig/git", root);
        } else {
            free(root);
            return NULL;
        }
        free(root);
    } else {
        // Global: ~/.rig/agent/npm/ or ~/.rig/agent/git/
        const char *agent_dir = config_agent_dir();
        if (type == PKG_NPM) {
            snprintf(buf, PATH_MAX, "%s/npm", agent_dir);
        } else if (type == PKG_GIT) {
            snprintf(buf, PATH_MAX, "%s/git", agent_dir);
        } else {
            return NULL;
        }
    }

    return strdup(buf);
}

// Extract package name from git URL for directory naming
static char *extract_repo_name(const char *url) {
    const char *last_slash = strrchr(url, '/');
    if (!last_slash) return strdup(url);

    const char *name = last_slash + 1;
    char *result = strdup(name);

    // Remove .git suffix if present
    size_t len = strlen(result);
    if (len > 4 && strcmp(result + len - 4, ".git") == 0) {
        result[len - 4] = '\0';
    }

    return result;
}

// Install a package
int package_install(const char *specifier, bool local) {
    if (!specifier) return -1;

    char *clean_name = NULL;
    PackageSourceType type = package_parse_specifier(specifier, &clean_name);
    if (!clean_name) return -1;

    char *install_dir = package_install_dir(type, local);
    if (!install_dir) {
        free(clean_name);
        return -1;
    }

    // Create install directory if it doesn't exist
    if (fs_mkdir_p(install_dir) != 0) {
        free(clean_name);
        free(install_dir);
        return -1;
    }

    int result = 0;

    if (type == PKG_NPM) {
        // npm install --prefix {install_dir} {name}
        Str cmd = str_new(256);
        str_appendf(&cmd, "npm install --prefix %s %s", install_dir, clean_name);

        ProcessOptions opts = {
            .command = cmd.data,
            .cwd = NULL,
            .env = NULL,
            .timeout_ms = 120000,  // 2 minute timeout
            .on_stdout = NULL,
            .on_stderr = NULL,
            .ctx = NULL,
            .abort_flag = NULL,
        };

        ProcessResult proc_result = {0};
        if (process_run(&opts, &proc_result) != 0 || proc_result.exit_code != 0) {
            result = -1;
        }

        str_free(&cmd);

    } else if (type == PKG_GIT) {
        // git clone {url} {install_dir}/{repo-name}
        char *repo_name = extract_repo_name(clean_name);
        char target_path[PATH_MAX];
        snprintf(target_path, PATH_MAX, "%s/%s", install_dir, repo_name);

        // Clone repository
        Str clone_cmd = str_new(256);
        str_appendf(&clone_cmd, "git clone %s %s", clean_name, target_path);

        ProcessOptions clone_opts = {
            .command = clone_cmd.data,
            .cwd = NULL,
            .env = NULL,
            .timeout_ms = 120000,
            .on_stdout = NULL,
            .on_stderr = NULL,
            .ctx = NULL,
            .abort_flag = NULL,
        };

        ProcessResult clone_result = {0};
        if (process_run(&clone_opts, &clone_result) != 0 || clone_result.exit_code != 0) {
            result = -1;
        } else {
            // Check if package.json exists, run npm install if it does
            char pkg_json_path[PATH_MAX];
            snprintf(pkg_json_path, PATH_MAX, "%s/package.json", target_path);

            if (fs_exists(pkg_json_path)) {
                Str npm_cmd = str_new(128);
                str_appendf(&npm_cmd, "npm install");

                ProcessOptions npm_opts = {
                    .command = npm_cmd.data,
                    .cwd = target_path,
                    .env = NULL,
                    .timeout_ms = 120000,
                    .on_stdout = NULL,
                    .on_stderr = NULL,
                    .ctx = NULL,
                    .abort_flag = NULL,
                };

                ProcessResult npm_result = {0};
                if (process_run(&npm_opts, &npm_result) != 0 || npm_result.exit_code != 0) {
                    result = -1;
                }

                str_free(&npm_cmd);
            }
        }

        str_free(&clone_cmd);
        free(repo_name);

    } else if (type == PKG_LOCAL) {
        // Just validate path exists
        char *expanded = fs_expand_home(clean_name);
        if (!expanded || !fs_exists(expanded)) {
            result = -1;
        }
        free(expanded);
    }

    free(clean_name);
    free(install_dir);
    return result;
}

// Remove a package
int package_remove(const char *specifier, bool local) {
    if (!specifier) return -1;

    char *clean_name = NULL;
    PackageSourceType type = package_parse_specifier(specifier, &clean_name);
    if (!clean_name) return -1;

    char *install_dir = package_install_dir(type, local);
    if (!install_dir) {
        free(clean_name);
        return -1;
    }

    int result = 0;

    if (type == PKG_NPM) {
        // npm uninstall --prefix {dir} {name}
        Str cmd = str_new(256);
        str_appendf(&cmd, "npm uninstall --prefix %s %s", install_dir, clean_name);

        ProcessOptions opts = {
            .command = cmd.data,
            .cwd = NULL,
            .env = NULL,
            .timeout_ms = 60000,
            .on_stdout = NULL,
            .on_stderr = NULL,
            .ctx = NULL,
            .abort_flag = NULL,
        };

        ProcessResult proc_result = {0};
        if (process_run(&opts, &proc_result) != 0 || proc_result.exit_code != 0) {
            result = -1;
        }

        str_free(&cmd);

    } else if (type == PKG_GIT) {
        // rm -rf {dir}/{repo-name}
        char *repo_name = extract_repo_name(clean_name);
        char target_path[PATH_MAX];
        snprintf(target_path, PATH_MAX, "%s/%s", install_dir, repo_name);

        Str cmd = str_new(256);
        str_appendf(&cmd, "rm -rf %s", target_path);

        ProcessOptions opts = {
            .command = cmd.data,
            .cwd = NULL,
            .env = NULL,
            .timeout_ms = 30000,
            .on_stdout = NULL,
            .on_stderr = NULL,
            .ctx = NULL,
            .abort_flag = NULL,
        };

        ProcessResult proc_result = {0};
        if (process_run(&opts, &proc_result) != 0 || proc_result.exit_code != 0) {
            result = -1;
        }

        str_free(&cmd);
        free(repo_name);

    } else if (type == PKG_LOCAL) {
        // LOCAL packages are references only, nothing to remove from filesystem
        result = 0;
    }

    free(clean_name);
    free(install_dir);
    return result;
}

// Helper struct for readdir callback
typedef struct {
    Package *packages;
    int count;
    int capacity;
    PackageSourceType source;
    char *base_dir;
} ListContext;

static void list_callback(const char *dir, const char *name, bool is_dir, void *ctx) {
    ListContext *lctx = (ListContext *)ctx;

    // Skip . and .. entries
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return;
    }

    // For NPM, list node_modules subdirectories
    // For GIT, list direct subdirectories
    if (!is_dir) return;

    // Skip special directories
    if (name[0] == '.') return;

    // Expand capacity if needed
    if (lctx->count >= lctx->capacity) {
        lctx->capacity = (lctx->capacity == 0) ? 8 : lctx->capacity * 2;
        lctx->packages = realloc(lctx->packages, lctx->capacity * sizeof(Package));
    }

    Package *pkg = &lctx->packages[lctx->count++];
    pkg->source = lctx->source;

    // Build full path
    char full_path[PATH_MAX];
    snprintf(full_path, PATH_MAX, "%s/%s", dir, name);
    pkg->local_path = strdup(full_path);
    pkg->name = strdup(name);

    // Build specifier
    if (lctx->source == PKG_NPM) {
        char spec[PATH_MAX];
        snprintf(spec, PATH_MAX, "npm:%s", name);
        pkg->specifier = strdup(spec);
    } else if (lctx->source == PKG_GIT) {
        char spec[PATH_MAX];
        snprintf(spec, PATH_MAX, "git:%s", name);
        pkg->specifier = strdup(spec);
    } else {
        pkg->specifier = strdup(full_path);
    }

    // Check if pinned (has version in package name)
    pkg->pinned = (strchr(name, '@') != NULL);
}

// List installed packages
Package *package_list(int *count) {
    if (!count) return NULL;

    ListContext ctx = {
        .packages = NULL,
        .count = 0,
        .capacity = 0,
        .source = PKG_NPM,
        .base_dir = NULL,
    };

    // List global NPM packages
    char *npm_global = package_install_dir(PKG_NPM, false);
    if (npm_global) {
        char node_modules[PATH_MAX];
        snprintf(node_modules, PATH_MAX, "%s/node_modules", npm_global);

        if (fs_is_dir(node_modules)) {
            ctx.source = PKG_NPM;
            ctx.base_dir = npm_global;
            fs_readdir(node_modules, list_callback, &ctx);
        }
        free(npm_global);
    }

    // List local NPM packages
    char *npm_local = package_install_dir(PKG_NPM, true);
    if (npm_local) {
        char node_modules[PATH_MAX];
        snprintf(node_modules, PATH_MAX, "%s/node_modules", npm_local);

        if (fs_is_dir(node_modules)) {
            ctx.source = PKG_NPM;
            ctx.base_dir = npm_local;
            fs_readdir(node_modules, list_callback, &ctx);
        }
        free(npm_local);
    }

    // List global GIT packages
    char *git_global = package_install_dir(PKG_GIT, false);
    if (git_global) {
        if (fs_is_dir(git_global)) {
            ctx.source = PKG_GIT;
            ctx.base_dir = git_global;
            fs_readdir(git_global, list_callback, &ctx);
        }
        free(git_global);
    }

    // List local GIT packages
    char *git_local = package_install_dir(PKG_GIT, true);
    if (git_local) {
        if (fs_is_dir(git_local)) {
            ctx.source = PKG_GIT;
            ctx.base_dir = git_local;
            fs_readdir(git_local, list_callback, &ctx);
        }
        free(git_local);
    }

    *count = ctx.count;
    return ctx.packages;
}

// Free package list
void package_list_free(Package *packages, int count) {
    if (!packages) return;

    for (int i = 0; i < count; i++) {
        free(packages[i].specifier);
        free(packages[i].local_path);
        free(packages[i].name);
    }

    free(packages);
}
