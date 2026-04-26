#ifndef PI_EXTENSION_H
#define PI_EXTENSION_H

#include "hooks.h"
#include "event_bus.h"
#include "ai/types.h"
#include "cjson/cJSON.h"
#include <stdbool.h>

#define PI_ABI_VERSION 1

#ifdef _WIN32
#define PI_EXPORT __declspec(dllexport)
#else
#define PI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct PiExtensionAPI PiExtensionAPI;

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

struct PiExtensionAPI {
    int abi_version;
    char *pi_version;

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

PiExtensionAPI *extension_api_create(void);
void extension_api_free(PiExtensionAPI *api);

int extension_api_register_tool(PiExtensionAPI *api, Tool *tool);
int extension_api_unregister_tool(PiExtensionAPI *api, const char *name);
Tool *extension_api_get_tool(PiExtensionAPI *api, const char *name);

int extension_api_register_command(PiExtensionAPI *api, const char *name,
                                    CommandHandler handler, void *ctx);

int extension_load_shared(PiExtensionAPI *api, const char *path);
int extension_load_lua(PiExtensionAPI *api, const char *path);
int extension_load_yaml_workflow(PiExtensionAPI *api, const char *path);

int extension_discover_and_load(PiExtensionAPI *api, const char *project_dir, const char *global_dir);

int extension_state_save(PiExtensionAPI *api);
int extension_state_load(PiExtensionAPI *api);

#endif
