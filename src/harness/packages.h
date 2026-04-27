#ifndef RIG_HARNESS_PACKAGES_H
#define RIG_HARNESS_PACKAGES_H

#include <stdbool.h>
#include "cjson/cJSON.h"

typedef enum {
    PKG_NPM,
    PKG_GIT,
    PKG_LOCAL,
} PackageSourceType;

typedef struct {
    PackageSourceType source;
    char *specifier;        // "npm:@foo/bar@1.2.3" or "git:github.com/user/repo"
    char *local_path;       // resolved install path
    char *name;             // display name
    bool pinned;            // versioned = pinned, skip updates
} Package;

// Install a package. specifier: "npm:name", "git:url", "/path"
// local: true = project-local (.rig/), false = global (~/.rig/agent/)
int package_install(const char *specifier, bool local);

// Remove a package
int package_remove(const char *specifier, bool local);

// List installed packages. Returns malloc'd array.
Package *package_list(int *count);

// Free package list
void package_list_free(Package *packages, int count);

// Parse a package specifier into type + clean name
PackageSourceType package_parse_specifier(const char *specifier, char **clean_name);

// Get the install directory for a package source type
char *package_install_dir(PackageSourceType type, bool local);

#endif
