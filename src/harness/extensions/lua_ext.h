#ifndef RIG_LUA_EXT_H
#define RIG_LUA_EXT_H

#include "extension.h"
#include "agent/agent.h"
#include <stdbool.h>
#include <pthread.h>

typedef struct LuaExtState LuaExtState;

/* Runtime context — set by interactive.c so Lua can reach agent internals */
typedef struct {
    AgentState *agent;
    void *store;          /* LineStore* */
    const Model *model;
    char *api_key;
    char *cwd;
    pthread_mutex_t *mutex;
    volatile bool *running;
} RigLuaContext;

LuaExtState *lua_ext_create(RigExtensionAPI *api);
void lua_ext_free(LuaExtState *state);

void lua_ext_set_context(LuaExtState *state, RigLuaContext *ctx);

int lua_ext_load_file(LuaExtState *state, const char *path);
int lua_ext_call_init(LuaExtState *state);
int lua_ext_eval(LuaExtState *state, const char *code, char **result);

int lua_ext_set_var(LuaExtState *state, const char *name, const char *value);
int lua_ext_set_var_json(LuaExtState *state, const char *name, cJSON *value);
char *lua_ext_get_var(LuaExtState *state, const char *name);

bool lua_ext_is_loaded(LuaExtState *state);

/* Call a lua tool handler by name (for agent tool execution bridge) */
int lua_ext_call_tool(LuaExtState *state, const char *name,
                      cJSON *params, char **result);

#endif
