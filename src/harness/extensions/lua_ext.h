#ifndef PI_LUA_EXT_H
#define PI_LUA_EXT_H

#include "extension.h"
#include <stdbool.h>

typedef struct LuaExtState LuaExtState;

LuaExtState *lua_ext_create(PiExtensionAPI *api);
void lua_ext_free(LuaExtState *state);

int lua_ext_load_file(LuaExtState *state, const char *path);
int lua_ext_call_init(LuaExtState *state);
int lua_ext_eval(LuaExtState *state, const char *code, char **result);

int lua_ext_set_var(LuaExtState *state, const char *name, const char *value);
int lua_ext_set_var_json(LuaExtState *state, const char *name, cJSON *value);
char *lua_ext_get_var(LuaExtState *state, const char *name);

bool lua_ext_is_loaded(LuaExtState *state);

#endif
