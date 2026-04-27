#ifndef RIG_EXTENSION_H
#define RIG_EXTENSION_H

#include "hooks.h"
#include "event_bus.h"
#include "ai/types.h"
#include "cjson/cJSON.h"
#include <stdbool.h>

#define RIG_ABI_VERSION 1

#ifdef _WIN32
#define RIG_EXPORT __declspec(dllexport)
#else
#define RIG_EXPORT __attribute__((visibility("default")))
#endif

typedef struct RigExtensionAPI RigExtensionAPI;

typedef int (*CommandHandler)(const char **args, int argc, void *ctx);

typedef struct {
    char *name;
    CommandHandler handler;
    void *ctx;
} RegisteredCommand;

typedef struct {
    char *name;
    char *path;
    void *dl_handle;
    void *lua_state;
    bool is_lua;
    bool is_yaml;
    cJSON *manifest;
    char **depends;
    int depends_count;
} Extension;

struct RigExtensionAPI {
    int abi_version;
    char *rig_version;

    HookChain *hooks;
    EventBus *bus;

    Tool **tools;
    int tool_count;
    int tool_capacity;

    RegisteredCommand *commands;
    int command_count;
    int command_capacity;

    Extension **extensions;
    int extension_count;
    int extension_capacity;

    cJSON *settings;
    cJSON *state;
    char *state_path;
};

RigExtensionAPI *extension_api_create(void);
void extension_api_free(RigExtensionAPI *api);

int extension_api_register_tool(RigExtensionAPI *api, Tool *tool);
int extension_api_unregister_tool(RigExtensionAPI *api, const char *name);
Tool *extension_api_get_tool(RigExtensionAPI *api, const char *name);

int extension_api_register_command(RigExtensionAPI *api, const char *name,
                                    CommandHandler handler, void *ctx);

int extension_load_shared(RigExtensionAPI *api, const char *path);
int extension_load_lua(RigExtensionAPI *api, const char *path);
int extension_load_yaml_workflow(RigExtensionAPI *api, const char *path);

int extension_discover_and_load(RigExtensionAPI *api, const char *project_dir, const char *global_dir);

int extension_state_save(RigExtensionAPI *api);
int extension_state_load(RigExtensionAPI *api);

#endif
