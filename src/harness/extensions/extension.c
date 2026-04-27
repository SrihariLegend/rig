#include "extension.h"
#include "lua_ext.h"
#include "util/fs.h"
#include "util/json.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>

RigExtensionAPI *extension_api_create(void) {
    RigExtensionAPI *api = calloc(1, sizeof(RigExtensionAPI));
    if (!api) return NULL;

    api->abi_version = RIG_ABI_VERSION;
    api->rig_version = strdup("0.1.0");
    api->hooks = hook_chain_create();
    api->bus = event_bus_create();

    api->tool_capacity = 32;
    api->tools = calloc(api->tool_capacity, sizeof(Tool *));
    api->command_capacity = 16;
    api->commands = calloc(api->command_capacity, sizeof(RegisteredCommand));
    api->extension_capacity = 16;
    api->extensions = calloc(api->extension_capacity, sizeof(Extension *));

    api->settings = cJSON_CreateObject();
    api->state = cJSON_CreateObject();

    if (!api->hooks || !api->bus || !api->tools || !api->commands || !api->extensions) {
        extension_api_free(api);
        return NULL;
    }

    return api;
}

void extension_api_free(RigExtensionAPI *api) {
    if (!api) return;

    free(api->rig_version);
    hook_chain_free(api->hooks);
    event_bus_free(api->bus);

    for (int i = 0; i < api->extension_count; i++) {
        Extension *ext = api->extensions[i];
        if (ext) {
            if (ext->dl_handle) dlclose(ext->dl_handle);
            if (ext->lua_state) lua_ext_free((LuaExtState *)ext->lua_state);
            free(ext->name);
            free(ext->path);
            for (int j = 0; j < ext->depends_count; j++) free(ext->depends[j]);
            free(ext->depends);
            if (ext->manifest) cJSON_Delete(ext->manifest);
            free(ext);
        }
    }
    free(api->extensions);

    free(api->tools);

    for (int i = 0; i < api->command_count; i++) {
        free(api->commands[i].name);
    }
    free(api->commands);

    if (api->settings) cJSON_Delete(api->settings);
    if (api->state) cJSON_Delete(api->state);
    free(api->state_path);
    free(api);
}

int extension_api_register_tool(RigExtensionAPI *api, Tool *tool) {
    if (!api || !tool) return -1;

    if (api->tool_count >= api->tool_capacity) {
        int new_cap = api->tool_capacity * 2;
        Tool **new_tools = realloc(api->tools, new_cap * sizeof(Tool *));
        if (!new_tools) return -1;
        api->tools = new_tools;
        api->tool_capacity = new_cap;
    }

    api->tools[api->tool_count++] = tool;
    return 0;
}

int extension_api_unregister_tool(RigExtensionAPI *api, const char *name) {
    if (!api || !name) return -1;

    for (int i = 0; i < api->tool_count; i++) {
        if (api->tools[i] && strcmp(api->tools[i]->name, name) == 0) {
            if (i < api->tool_count - 1) {
                memmove(&api->tools[i], &api->tools[i + 1],
                        (api->tool_count - i - 1) * sizeof(Tool *));
            }
            api->tool_count--;
            return 0;
        }
    }
    return -1;
}

Tool *extension_api_get_tool(RigExtensionAPI *api, const char *name) {
    if (!api || !name) return NULL;

    for (int i = 0; i < api->tool_count; i++) {
        if (api->tools[i] && strcmp(api->tools[i]->name, name) == 0) {
            return api->tools[i];
        }
    }
    return NULL;
}

int extension_api_register_command(RigExtensionAPI *api, const char *name,
                                    CommandHandler handler, void *ctx) {
    if (!api || !name || !handler) return -1;

    if (api->command_count >= api->command_capacity) {
        int new_cap = api->command_capacity * 2;
        RegisteredCommand *new_cmds = realloc(api->commands, new_cap * sizeof(RegisteredCommand));
        if (!new_cmds) return -1;
        api->commands = new_cmds;
        api->command_capacity = new_cap;
    }

    api->commands[api->command_count++] = (RegisteredCommand){
        .name = strdup(name),
        .handler = handler,
        .ctx = ctx,
    };
    return 0;
}

typedef void (*ExtInitFn)(RigExtensionAPI *);
typedef int (*AbiVersionFn)(void);
typedef const char **(*DependsFn)(int *);

int extension_load_shared(RigExtensionAPI *api, const char *path) {
    if (!api || !path) return -1;

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        LOG_ERROR("dlopen failed: %s", dlerror());
        return -1;
    }

    AbiVersionFn abi_fn = (AbiVersionFn)dlsym(handle, "rig_abi_version");
    if (abi_fn) {
        int version = abi_fn();
        if (version != RIG_ABI_VERSION) {
            LOG_ERROR("ABI mismatch: extension=%d, rig=%d", version, RIG_ABI_VERSION);
            dlclose(handle);
            return -1;
        }
    }

    ExtInitFn init_fn = (ExtInitFn)dlsym(handle, "rig_extension_init");
    if (!init_fn) {
        LOG_ERROR("No rig_extension_init in %s", path);
        dlclose(handle);
        return -1;
    }

    Extension *ext = calloc(1, sizeof(Extension));
    if (!ext) {
        dlclose(handle);
        return -1;
    }

    const char *filename = strrchr(path, '/');
    ext->name = strdup(filename ? filename + 1 : path);
    ext->path = strdup(path);
    ext->dl_handle = handle;

    DependsFn deps_fn = (DependsFn)dlsym(handle, "rig_extension_depends");
    if (deps_fn) {
        int dep_count;
        const char **deps = deps_fn(&dep_count);
        if (deps && dep_count > 0) {
            ext->depends = calloc(dep_count, sizeof(char *));
            for (int i = 0; i < dep_count; i++) {
                ext->depends[i] = strdup(deps[i]);
            }
            ext->depends_count = dep_count;
        }
    }

    if (api->extension_count >= api->extension_capacity) {
        int new_cap = api->extension_capacity * 2;
        Extension **new_exts = realloc(api->extensions, new_cap * sizeof(Extension *));
        if (!new_exts) {
            free(ext->name);
            free(ext->path);
            dlclose(handle);
            free(ext);
            return -1;
        }
        api->extensions = new_exts;
        api->extension_capacity = new_cap;
    }
    api->extensions[api->extension_count++] = ext;

    init_fn(api);
    return 0;
}

int extension_load_lua(RigExtensionAPI *api, const char *path) {
    if (!api || !path) return -1;

    LuaExtState *lua = lua_ext_create(api);
    if (!lua) {
        LOG_ERROR("Failed to create Lua state for %s", path);
        return -1;
    }

    if (lua_ext_load_file(lua, path) != 0) {
        LOG_ERROR("Failed to load Lua file %s", path);
        lua_ext_free(lua);
        return -1;
    }

    if (lua_ext_call_init(lua) != 0) {
        LOG_ERROR("Failed to call init in %s", path);
        lua_ext_free(lua);
        return -1;
    }

    Extension *ext = calloc(1, sizeof(Extension));
    if (!ext) {
        lua_ext_free(lua);
        return -1;
    }

    const char *filename = strrchr(path, '/');
    ext->name = strdup(filename ? filename + 1 : path);
    ext->path = strdup(path);
    ext->is_lua = true;
    ext->lua_state = lua;

    if (api->extension_count >= api->extension_capacity) {
        int new_cap = api->extension_capacity * 2;
        Extension **new_exts = realloc(api->extensions, new_cap * sizeof(Extension *));
        if (!new_exts) {
            free(ext->name);
            free(ext->path);
            lua_ext_free(lua);
            free(ext);
            return -1;
        }
        api->extensions = new_exts;
        api->extension_capacity = new_cap;
    }
    api->extensions[api->extension_count++] = ext;

    return 0;
}

int extension_load_yaml_workflow(RigExtensionAPI *api, const char *path) {
    if (!api || !path) return -1;

    Extension *ext = calloc(1, sizeof(Extension));
    if (!ext) return -1;

    const char *filename = strrchr(path, '/');
    ext->name = strdup(filename ? filename + 1 : path);
    ext->path = strdup(path);
    ext->is_yaml = true;

    if (api->extension_count >= api->extension_capacity) {
        int new_cap = api->extension_capacity * 2;
        Extension **new_exts = realloc(api->extensions, new_cap * sizeof(Extension *));
        if (!new_exts) {
            free(ext->name);
            free(ext->path);
            free(ext);
            return -1;
        }
        api->extensions = new_exts;
        api->extension_capacity = new_cap;
    }
    api->extensions[api->extension_count++] = ext;

    return 0;
}

static void discover_dir(RigExtensionAPI *api, const char *dir, const char *subdir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, subdir);

    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full[1280];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);

        size_t len = strlen(ent->d_name);
        if (len > 3 && strcmp(ent->d_name + len - 3, ".so") == 0) {
            extension_load_shared(api, full);
        } else if (len > 6 && strcmp(ent->d_name + len - 6, ".dylib") == 0) {
            extension_load_shared(api, full);
        } else if (len > 4 && strcmp(ent->d_name + len - 4, ".lua") == 0) {
            extension_load_lua(api, full);
        } else if (len > 5 && strcmp(ent->d_name + len - 5, ".yaml") == 0) {
            extension_load_yaml_workflow(api, full);
        } else if (len > 4 && strcmp(ent->d_name + len - 4, ".yml") == 0) {
            extension_load_yaml_workflow(api, full);
        }
    }
    closedir(d);
}

int extension_discover_and_load(RigExtensionAPI *api, const char *project_dir, const char *global_dir) {
    if (!api) return -1;

    if (global_dir) {
        discover_dir(api, global_dir, "extensions");
        discover_dir(api, global_dir, "workflows");
    }

    if (project_dir) {
        discover_dir(api, project_dir, "extensions");
        discover_dir(api, project_dir, "workflows");
    }

    return 0;
}

int extension_state_save(RigExtensionAPI *api) {
    if (!api || !api->state_path || !api->state) return -1;

    char *json_str = cJSON_Print(api->state);
    if (!json_str) return -1;

    int result = fs_write_file(api->state_path, json_str, strlen(json_str));
    free(json_str);
    return result;
}

int extension_state_load(RigExtensionAPI *api) {
    if (!api || !api->state_path) return -1;

    size_t len;
    char *content = fs_read_file(api->state_path, &len);
    if (!content) return -1;

    cJSON *state = cJSON_Parse(content);
    free(content);
    if (!state) return -1;

    if (api->state) cJSON_Delete(api->state);
    api->state = state;
    return 0;
}
