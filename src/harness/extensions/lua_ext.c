#include "lua_ext.h"
#include "util/log.h"
#include "util/str.h"
#include "util/process.h"
#include "util/fs.h"
#include "agent/agent.h"
#include "ai/registry.h"
#include "harness/system_prompt.h"
#include "tui/linestore.h"
#include "cjson/cJSON.h"

#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#define LUA_MEM_LIMIT (50 * 1024 * 1024)
#define LUA_INSTRUCTION_LIMIT 100000000
#define LUA_INIT_INSTRUCTION_LIMIT 1000000

#define REG_API_KEY   "rig_extension_api"
#define REG_STATE_KEY "rig_lua_ext_state"
#define REG_CTX_KEY   "rig_lua_context"
#define REG_TOOLS_KEY "rig_lua_tools"

struct LuaExtState {
    lua_State *L;
    RigExtensionAPI *api;
    RigLuaContext *ctx;
    size_t mem_used;
    bool loaded;
    bool sandboxed;
    int next_hook_id;
    pthread_mutex_t lua_mutex;

    /* Track allocations for cleanup */
    void **allocs;
    int alloc_count;
    int alloc_cap;
};

static void track_alloc(LuaExtState *st, void *ptr) {
    if (!st || !ptr) return;
    if (st->alloc_count >= st->alloc_cap) {
        int new_cap = st->alloc_cap == 0 ? 16 : st->alloc_cap * 2;
        void **na = realloc(st->allocs, (size_t)new_cap * sizeof(void *));
        if (!na) return;
        st->allocs = na;
        st->alloc_cap = new_cap;
    }
    st->allocs[st->alloc_count++] = ptr;
}

/* ============================================================
 *  Memory allocator
 * ============================================================ */

static void *lua_alloc_tracked(void *ud, void *ptr, size_t osize, size_t nsize) {
    LuaExtState *state = (LuaExtState *)ud;
    if (nsize == 0) {
        state->mem_used -= osize;
        free(ptr);
        return NULL;
    }
    if (state->mem_used - osize + nsize > LUA_MEM_LIMIT) return NULL;
    void *new_ptr = realloc(ptr, nsize);
    if (new_ptr) state->mem_used = state->mem_used - osize + nsize;
    return new_ptr;
}

static void lua_hook_count(lua_State *L, lua_Debug *ar) {
    (void)ar;
    luaL_error(L, "instruction limit exceeded");
}

/* ============================================================
 *  Sandbox
 * ============================================================ */

static void sandbox_lua(lua_State *L) {
    lua_pushnil(L); lua_setglobal(L, "os");
    lua_pushnil(L); lua_setglobal(L, "io");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "debug");
    luaL_dostring(L, "package.cpath = ''");
    luaL_dostring(L, "package.loadlib = nil");
}

/* ============================================================
 *  cJSON <-> Lua
 * ============================================================ */

static void push_cjson(lua_State *L, cJSON *json) {
    if (!json || cJSON_IsNull(json)) { lua_pushnil(L); return; }
    if (cJSON_IsBool(json)) { lua_pushboolean(L, cJSON_IsTrue(json)); return; }
    if (cJSON_IsNumber(json)) { lua_pushnumber(L, json->valuedouble); return; }
    if (cJSON_IsString(json)) { lua_pushstring(L, json->valuestring); return; }
    if (cJSON_IsArray(json)) {
        lua_newtable(L);
        int idx = 1;
        cJSON *item;
        cJSON_ArrayForEach(item, json) { push_cjson(L, item); lua_rawseti(L, -2, idx++); }
        return;
    }
    if (cJSON_IsObject(json)) {
        lua_newtable(L);
        cJSON *item;
        cJSON_ArrayForEach(item, json) {
            lua_pushstring(L, item->string);
            push_cjson(L, item);
            lua_rawset(L, -3);
        }
        return;
    }
    lua_pushnil(L);
}

static cJSON *lua_to_cjson(lua_State *L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
    case LUA_TNIL: return cJSON_CreateNull();
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) ? cJSON_CreateTrue() : cJSON_CreateFalse();
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) return cJSON_CreateNumber((double)lua_tointeger(L, idx));
        return cJSON_CreateNumber(lua_tonumber(L, idx));
    case LUA_TSTRING: return cJSON_CreateString(lua_tostring(L, idx));
    case LUA_TTABLE: {
        lua_rawgeti(L, idx, 1);
        bool is_array = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (is_array) {
            cJSON *arr = cJSON_CreateArray();
            int len = (int)lua_rawlen(L, idx);
            for (int i = 1; i <= len; i++) {
                lua_rawgeti(L, idx, i);
                cJSON_AddItemToArray(arr, lua_to_cjson(L, lua_gettop(L)));
                lua_pop(L, 1);
            }
            return arr;
        }
        cJSON *obj = cJSON_CreateObject();
        int abs = lua_absindex(L, idx);
        lua_pushnil(L);
        while (lua_next(L, abs) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING)
                cJSON_AddItemToObject(obj, lua_tostring(L, -2), lua_to_cjson(L, lua_gettop(L)));
            lua_pop(L, 1);
        }
        return obj;
    }
    default: return cJSON_CreateNull();
    }
}

/* ============================================================
 *  Registry helpers
 * ============================================================ */

static RigExtensionAPI *get_api(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, REG_API_KEY);
    RigExtensionAPI *api = (RigExtensionAPI *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return api;
}

static LuaExtState *get_state(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, REG_STATE_KEY);
    LuaExtState *st = (LuaExtState *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return st;
}

static RigLuaContext *get_ctx(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, REG_CTX_KEY);
    RigLuaContext *ctx = (RigLuaContext *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

/* ============================================================
 *  Primitive 1: rig.exec(cmd, opts?) → {ok, stdout, exit_code, timed_out}
 * ============================================================ */

static void exec_collect(const char *data, size_t len, void *ctx) {
    str_append_len((Str *)ctx, data, len);
}

static int lua_rig_exec(lua_State *L) {
    const char *cmd = luaL_checkstring(L, 1);
    RigLuaContext *ctx = get_ctx(L);

    int timeout = 30000;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "timeout");
        if (lua_isinteger(L, -1)) timeout = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    Str out = str_new(4096);
    ProcessOptions opts = {
        .command = cmd,
        .cwd = ctx ? ctx->cwd : NULL,
        .timeout_ms = timeout,
        .on_stdout = exec_collect,
        .on_stderr = exec_collect,
        .ctx = &out,
    };
    ProcessResult result;
    process_run(&opts, &result);

    lua_newtable(L);
    lua_pushboolean(L, result.exit_code == 0);
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, out.data ? out.data : "");
    lua_setfield(L, -2, "stdout");
    lua_pushinteger(L, result.exit_code);
    lua_setfield(L, -2, "exit_code");
    lua_pushboolean(L, result.timed_out);
    lua_setfield(L, -2, "timed_out");

    str_free(&out);
    return 1;
}

/* ============================================================
 *  Primitive 2: rig.completion(params) → string
 * ============================================================ */

typedef struct {
    Str *text;
    bool done;
} CompletionBridge;

static void completion_stream_cb(StreamEvent *event, void *ud) {
    CompletionBridge *b = (CompletionBridge *)ud;
    if (event->type == EVENT_TEXT_DELTA && event->delta) {
        str_append(b->text, event->delta);
    }
    if (event->type == EVENT_DONE) {
        b->done = true;
    }
    if (event->type == EVENT_ERROR && event->error_message) {
        str_append(b->text, "[error: ");
        str_append(b->text, event->error_message);
        str_append(b->text, "]");
        b->done = true;
    }
}

static int lua_rig_completion(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    RigLuaContext *ctx = get_ctx(L);
    if (!ctx || !ctx->model || !ctx->api_key)
        return luaL_error(L, "no model/key configured");

    lua_getfield(L, 1, "system");
    const char *system_prompt = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
    lua_pop(L, 1);

    lua_getfield(L, 1, "messages");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "completion requires 'messages' table");
    }

    int msg_count = (int)lua_rawlen(L, -1);
    Message **messages = calloc(msg_count > 0 ? msg_count : 1, sizeof(Message *));
    if (!messages) { lua_pop(L, 1); return luaL_error(L, "out of memory"); }

    int actual_count = 0;
    for (int i = 1; i <= msg_count; i++) {
        lua_rawgeti(L, -1, i);
        lua_getfield(L, -1, "role");
        lua_getfield(L, -2, "content");
        const char *role = lua_tostring(L, -2);
        const char *content = lua_tostring(L, -1);
        lua_pop(L, 3);

        if (!role || !content) continue;
        Message *msg = NULL;
        if (strcmp(role, "user") == 0) {
            msg = message_create_user(content);
        } else if (strcmp(role, "assistant") == 0) {
            msg = message_create_assistant();
            if (msg) message_add_content(msg, content_text(content, NULL));
        }
        if (msg) messages[actual_count++] = msg;
    }
    lua_pop(L, 1);

    const Model *model = ctx->model;
    Str response = str_new(4096);
    CompletionBridge bridge = { .text = &response, .done = false };

    int max_tokens = model->max_tokens;
    lua_getfield(L, 1, "max_tokens");
    if (lua_isinteger(L, -1)) max_tokens = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    /* Flatten Message** to Message* (shallow copy for ai_stream_simple) */
    Message *flat = NULL;
    if (actual_count > 0) {
        flat = malloc((size_t)actual_count * sizeof(Message));
        for (int i = 0; i < actual_count; i++) flat[i] = *messages[i];
    }

    SimpleStreamOptions sopts = {
        .base = {
            .temperature = -1.0,
            .max_tokens = max_tokens,
            .api_key = ctx->api_key,
            .timeout_ms = 120000,
        },
        .reasoning = THINKING_OFF,
    };

    if (actual_count == 0) {
        free(flat);
        free(messages);
        str_free(&response);
        return luaL_error(L, "completion: no valid messages");
    }

    ai_stream_simple(model, flat, actual_count,
                     system_prompt, NULL, 0,
                     &sopts, completion_stream_cb, &bridge);

    free(flat);
    for (int i = 0; i < actual_count; i++) message_free(messages[i]);
    free(messages);

    lua_pushstring(L, response.data ? response.data : "");
    str_free(&response);
    return 1;
}

/* ============================================================
 *  Primitive 3: rig.print(text, opts?)
 * ============================================================ */

static int lua_rig_print(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    RigLuaContext *ctx = get_ctx(L);
    if (!ctx || !ctx->store) return luaL_error(L, "no TUI context");

    LineStore *store = (LineStore *)ctx->store;
    bool is_error = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "error");
        is_error = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }

    if (ctx->mutex) pthread_mutex_lock(ctx->mutex);
    if (is_error) linestore_add_error(store, text);
    else linestore_add_system(store, text);
    if (ctx->mutex) pthread_mutex_unlock(ctx->mutex);

    return 0;
}

/* ============================================================
 *  Primitive 4: rig.input(prompt) → string
 * ============================================================ */

static int lua_rig_input(lua_State *L) {
    const char *prompt = luaL_optstring(L, 1, "> ");
    RigLuaContext *ctx = get_ctx(L);
    if (!ctx || !ctx->store) return luaL_error(L, "no TUI context");

    LineStore *store = (LineStore *)ctx->store;
    if (ctx->mutex) pthread_mutex_lock(ctx->mutex);
    linestore_add_system(store, prompt);
    if (ctx->mutex) pthread_mutex_unlock(ctx->mutex);

    Str input = str_new(256);
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };

    while (1) {
        int ready = poll(&pfd, 1, 100);
        if (ready > 0 && (pfd.revents & POLLIN)) {
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n <= 0) break;
            if (ch == '\r' || ch == '\n') break;
            if (ch == '\x03') {
                str_free(&input);
                lua_pushnil(L);
                return 1;
            }
            if (ch == 127 || ch == '\b') {
                if (input.len > 0) input.len--;
                continue;
            }
            if (ch >= 32) str_append_char(&input, ch);
        }
    }

    if (input.data) input.data[input.len] = '\0';
    lua_pushstring(L, input.data ? input.data : "");
    str_free(&input);
    return 1;
}

/* ============================================================
 *  Primitive 5: rig.hook(event, fn, priority?) → handle
 * ============================================================ */

typedef struct { lua_State *L; int ref; pthread_mutex_t *lua_mutex; } LuaHookCtx;

static void show_lua_error(lua_State *L, const char *prefix, const char *err) {
    RigLuaContext *ctx = get_ctx(L);
    if (ctx && ctx->store) {
        LineStore *store = (LineStore *)ctx->store;
        char buf[512];
        snprintf(buf, sizeof(buf), "[ext] %s: %s", prefix, err ? err : "unknown error");
        if (ctx->mutex) pthread_mutex_lock(ctx->mutex);
        linestore_add_error(store, buf);
        if (ctx->mutex) pthread_mutex_unlock(ctx->mutex);
    }
}

static bool lua_hook_bridge(const char *event, cJSON *data,
                            cJSON **result, void *ud) {
    LuaHookCtx *hctx = (LuaHookCtx *)ud;
    if (hctx->lua_mutex) pthread_mutex_lock(hctx->lua_mutex);

    lua_rawgeti(hctx->L, LUA_REGISTRYINDEX, hctx->ref);
    lua_pushstring(hctx->L, event);
    push_cjson(hctx->L, data);

    bool ok = true;
    if (lua_pcall(hctx->L, 2, 1, 0) != LUA_OK) {
        const char *err = lua_tostring(hctx->L, -1);
        LOG_ERROR("[lua] hook error: %s", err);
        show_lua_error(hctx->L, "hook error", err);
        lua_pop(hctx->L, 1);
    } else {
        if (lua_isboolean(hctx->L, -1)) ok = lua_toboolean(hctx->L, -1);
        else if (lua_istable(hctx->L, -1) && result) *result = lua_to_cjson(hctx->L, -1);
        lua_pop(hctx->L, 1);
    }

    if (hctx->lua_mutex) pthread_mutex_unlock(hctx->lua_mutex);
    return ok;
}

static int lua_rig_hook(lua_State *L) {
    RigExtensionAPI *api = get_api(L);
    LuaExtState *st = get_state(L);
    if (!api) return luaL_error(L, "no api");

    const char *event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int priority = (int)luaL_optinteger(L, 3, 100);

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    LuaHookCtx *hctx = malloc(sizeof(LuaHookCtx));
    if (!hctx) return luaL_error(L, "out of memory");
    hctx->L = L;
    hctx->ref = ref;
    hctx->lua_mutex = &st->lua_mutex;

    char name[64];
    snprintf(name, sizeof(name), "lua_hook_%d", ++st->next_hook_id);
    track_alloc(st, hctx);
    hook_chain_add(api->hooks, event, priority, lua_hook_bridge, hctx, name);

    lua_pushstring(L, name);
    return 1;
}

/* ============================================================
 *  Primitive 6: rig.unhook(handle) → bool
 * ============================================================ */

static int lua_rig_unhook(lua_State *L) {
    RigExtensionAPI *api = get_api(L);
    if (!api) return luaL_error(L, "no api");
    const char *name = luaL_checkstring(L, 1);
    lua_pushboolean(L, hook_chain_remove(api->hooks, name) == 0);
    return 1;
}

/* ============================================================
 *  Primitive 7: rig.get(ns, key?) → value
 * ============================================================ */

static int lua_rig_get(lua_State *L) {
    const char *ns = luaL_checkstring(L, 1);
    const char *key = luaL_optstring(L, 2, NULL);
    RigLuaContext *ctx = get_ctx(L);
    RigExtensionAPI *api = get_api(L);

    if (strcmp(ns, "config") == 0) {
        if (!key) {
            lua_newtable(L);
            if (ctx && ctx->model) {
                lua_pushstring(L, ctx->model->name); lua_setfield(L, -2, "model");
                lua_pushstring(L, ctx->model->id); lua_setfield(L, -2, "model_id");
                lua_pushstring(L, ctx->model->provider); lua_setfield(L, -2, "provider");
                lua_pushinteger(L, ctx->model->context_window); lua_setfield(L, -2, "context_window");
            }
            if (ctx && ctx->cwd) { lua_pushstring(L, ctx->cwd); lua_setfield(L, -2, "cwd"); }
            return 1;
        }
        if (ctx && ctx->model) {
            if (strcmp(key, "model") == 0) { lua_pushstring(L, ctx->model->name); return 1; }
            if (strcmp(key, "model_id") == 0) { lua_pushstring(L, ctx->model->id); return 1; }
            if (strcmp(key, "provider") == 0) { lua_pushstring(L, ctx->model->provider); return 1; }
        }
        if (ctx && ctx->cwd && strcmp(key, "cwd") == 0) { lua_pushstring(L, ctx->cwd); return 1; }
        lua_pushnil(L); return 1;
    }

    if (strcmp(ns, "messages") == 0) {
        if (!ctx || !ctx->agent) { lua_pushnil(L); return 1; }
        if (key && strcmp(key, "count") == 0) {
            lua_pushinteger(L, ctx->agent->message_count); return 1;
        }
        if (key && strcmp(key, "tokens") == 0) {
            /* Rough estimate: sum text lengths / 4 */
            size_t chars = 0;
            for (int i = 0; i < ctx->agent->message_count; i++) {
                Message *m = ctx->agent->messages[i];
                for (int j = 0; j < m->content_count; j++) {
                    if (m->content[j].type == CONTENT_TEXT && m->content[j].text.text)
                        chars += strlen(m->content[j].text.text);
                }
            }
            lua_pushinteger(L, (lua_Integer)(chars / 4));
            return 1;
        }
        /* Integer key: return single message (1-indexed) */
        if (key) {
            char *endp;
            long idx = strtol(key, &endp, 10);
            if (*endp == '\0' && idx >= 1 && idx <= ctx->agent->message_count) {
                Message *m = ctx->agent->messages[idx - 1];
                lua_newtable(L);
                const char *role = m->role == ROLE_USER ? "user" :
                                   m->role == ROLE_ASSISTANT ? "assistant" :
                                   m->role == ROLE_TOOL_RESULT ? "tool_result" : "unknown";
                lua_pushstring(L, role); lua_setfield(L, -2, "role");
                Str text = str_new(256);
                for (int j = 0; j < m->content_count; j++) {
                    if (m->content[j].type == CONTENT_TEXT && m->content[j].text.text)
                        str_append(&text, m->content[j].text.text);
                }
                lua_pushstring(L, text.data ? text.data : "");
                lua_setfield(L, -2, "content");
                str_free(&text);
                return 1;
            }
            if (*endp == '\0') { lua_pushnil(L); return 1; }
        }
        /* No key or non-numeric: return all messages */
        lua_newtable(L);
        for (int i = 0; i < ctx->agent->message_count; i++) {
            Message *m = ctx->agent->messages[i];
            lua_newtable(L);
            const char *role = m->role == ROLE_USER ? "user" :
                               m->role == ROLE_ASSISTANT ? "assistant" :
                               m->role == ROLE_TOOL_RESULT ? "tool_result" : "unknown";
            lua_pushstring(L, role); lua_setfield(L, -2, "role");
            Str text = str_new(256);
            for (int j = 0; j < m->content_count; j++) {
                if (m->content[j].type == CONTENT_TEXT && m->content[j].text.text)
                    str_append(&text, m->content[j].text.text);
            }
            lua_pushstring(L, text.data ? text.data : "");
            lua_setfield(L, -2, "content");
            str_free(&text);
            lua_rawseti(L, -2, i + 1);
        }
        return 1;
    }

    if (strcmp(ns, "tools") == 0) {
        if (!api) { lua_pushnil(L); return 1; }
        if (key) {
            Tool *t = extension_api_get_tool(api, key);
            if (t) {
                lua_newtable(L);
                lua_pushstring(L, t->name); lua_setfield(L, -2, "name");
                if (t->description) { lua_pushstring(L, t->description); lua_setfield(L, -2, "description"); }
                return 1;
            }
            lua_pushnil(L); return 1;
        }
        lua_newtable(L);
        for (int i = 0; i < api->tool_count; i++) {
            if (api->tools[i]) {
                lua_pushstring(L, api->tools[i]->name);
                lua_rawseti(L, -2, i + 1);
            }
        }
        return 1;
    }

    if (strcmp(ns, "settings") == 0) {
        if (api && api->settings) {
            push_cjson(L, key ? cJSON_GetObjectItem(api->settings, key) : api->settings);
        } else { lua_pushnil(L); }
        return 1;
    }

    if (strcmp(ns, "state") == 0) {
        if (api && api->state) {
            push_cjson(L, key ? cJSON_GetObjectItem(api->state, key) : api->state);
        } else { lua_pushnil(L); }
        return 1;
    }

    lua_pushnil(L); return 1;
}

/* ============================================================
 *  Primitive 8: rig.set(ns, key, value)
 * ============================================================ */

/* Command bridge for Lua-registered slash commands */
typedef struct { lua_State *L; int ref; pthread_mutex_t *lua_mutex; } LuaCmdCtx;

static int lua_cmd_bridge(const char **args, int argc, void *ud) {
    LuaCmdCtx *c = (LuaCmdCtx *)ud;
    if (c->lua_mutex) pthread_mutex_lock(c->lua_mutex);

    lua_rawgeti(c->L, LUA_REGISTRYINDEX, c->ref);
    lua_newtable(c->L);
    for (int i = 0; i < argc; i++) {
        lua_pushstring(c->L, args[i]);
        lua_rawseti(c->L, -2, i + 1);
    }
    int rc = 0;
    if (lua_pcall(c->L, 1, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(c->L, -1);
        LOG_ERROR("[lua] command error: %s", err);
        show_lua_error(c->L, "command error", err);
        lua_pop(c->L, 1);
        rc = -1;
    }

    if (c->lua_mutex) pthread_mutex_unlock(c->lua_mutex);
    return rc;
}

static int lua_rig_set(lua_State *L) {
    const char *ns = luaL_checkstring(L, 1);
    const char *key = luaL_checkstring(L, 2);
    RigExtensionAPI *api = get_api(L);
    RigLuaContext *ctx = get_ctx(L);
    LuaExtState *st = get_state(L);

    if (strcmp(ns, "tools") == 0) {
        if (!api) return luaL_error(L, "no api");
        if (lua_isnil(L, 3)) {
            extension_api_unregister_tool(api, key);
            return 0;
        }
        luaL_checktype(L, 3, LUA_TTABLE);

        lua_getfield(L, 3, "description");
        const char *desc = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);

        lua_getfield(L, 3, "params");
        cJSON *raw_params = lua_istable(L, -1) ? lua_to_cjson(L, -1) : NULL;
        lua_pop(L, 1);

        /* Build proper JSON Schema: {"type":"object","properties":{...}} */
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        if (raw_params) {
            if (cJSON_GetObjectItem(raw_params, "type")) {
                /* Already a proper schema */
                cJSON_Delete(params);
                params = raw_params;
            } else {
                /* Bare {key: {type: ...}} — wrap as properties */
                cJSON_AddItemToObject(params, "properties", raw_params);
            }
        }

        lua_getfield(L, 3, "instructions");
        const char *instructions = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
        lua_pop(L, 1);

        lua_getfield(L, 3, "run");
        if (lua_isfunction(L, -1)) {
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_getfield(L, LUA_REGISTRYINDEX, REG_TOOLS_KEY);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_setfield(L, LUA_REGISTRYINDEX, REG_TOOLS_KEY);
            }
            lua_pushinteger(L, ref);
            lua_setfield(L, -2, key);
            lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
        }

        extension_api_unregister_tool(api, key);
        Tool *tool = calloc(1, sizeof(Tool));
        if (!tool) { cJSON_Delete(params); return luaL_error(L, "out of memory"); }
        tool->name = strdup(key);
        tool->description = strdup(desc);
        tool->label = instructions ? strdup(instructions) : NULL;
        tool->parameters = params;
        extension_api_register_tool(api, tool);
        return 0;
    }

    if (strcmp(ns, "commands") == 0) {
        if (!api) return luaL_error(L, "no api");
        if (lua_isnil(L, 3)) return 0;
        luaL_checktype(L, 3, LUA_TFUNCTION);

        lua_pushvalue(L, 3);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);

        LuaCmdCtx *cctx = malloc(sizeof(LuaCmdCtx));
        if (!cctx) return luaL_error(L, "out of memory");
        cctx->L = L;
        cctx->ref = ref;
        cctx->lua_mutex = &st->lua_mutex;
        track_alloc(st, cctx);

        extension_api_register_command(api, key, lua_cmd_bridge, cctx);
        return 0;
    }

    if (strcmp(ns, "messages") == 0) {
        if (!ctx || !ctx->agent) return luaL_error(L, "no agent");
        if (strcmp(key, "append") == 0) {
            luaL_checktype(L, 3, LUA_TTABLE);
            lua_getfield(L, 3, "role");
            lua_getfield(L, 3, "content");
            const char *role = lua_tostring(L, -2);
            const char *content = lua_tostring(L, -1);
            lua_pop(L, 2);
            if (!role || !content) return luaL_error(L, "message needs role and content");

            Message *msg = NULL;
            if (strcmp(role, "user") == 0) msg = message_create_user(content);
            else if (strcmp(role, "assistant") == 0) {
                msg = message_create_assistant();
                if (msg) message_add_content(msg, content_text(content, NULL));
            }
            if (msg) agent_state_add_message(ctx->agent, msg);
            return 0;
        }
        if (strcmp(key, "clear") == 0) {
            agent_state_reset(ctx->agent);
            return 0;
        }
        if (strcmp(key, "splice") == 0) {
            /* rig.set("messages", "splice", {start=N, delete=N, messages={...}}) */
            luaL_checktype(L, 3, LUA_TTABLE);
            if (ctx->agent->is_streaming)
                return luaL_error(L, "cannot splice while streaming");

            lua_getfield(L, 3, "start");
            int start = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
            lua_pop(L, 1);
            if (start < 1) return luaL_error(L, "splice: start must be >= 1");
            start--; /* Convert to 0-indexed */

            lua_getfield(L, 3, "delete");
            int delete_count = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
            lua_pop(L, 1);
            if (delete_count < 0) return luaL_error(L, "splice: delete must be >= 0");

            /* Parse replacement messages */
            lua_getfield(L, 3, "messages");
            int insert_count = 0;
            Message **insert = NULL;
            if (lua_istable(L, -1)) {
                insert_count = (int)lua_rawlen(L, -1);
                if (insert_count > 0) {
                    insert = calloc((size_t)insert_count, sizeof(Message *));
                    if (!insert) { lua_pop(L, 1); return luaL_error(L, "out of memory"); }
                    int valid = 0;
                    for (int i = 1; i <= insert_count; i++) {
                        lua_rawgeti(L, -1, i);
                        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
                        lua_getfield(L, -1, "role");
                        lua_getfield(L, -2, "content");
                        const char *r = lua_tostring(L, -2);
                        const char *c = lua_tostring(L, -1);
                        lua_pop(L, 3);
                        if (!r || !c) continue;
                        Message *msg = NULL;
                        if (strcmp(r, "user") == 0) msg = message_create_user(c);
                        else if (strcmp(r, "assistant") == 0) {
                            msg = message_create_assistant();
                            if (msg) message_add_content(msg, content_text(c, NULL));
                        }
                        if (msg) insert[valid++] = msg;
                    }
                    insert_count = valid;
                }
            }
            lua_pop(L, 1);

            int rc = agent_state_splice(ctx->agent, start, delete_count,
                                        insert, insert_count);
            free(insert);
            if (rc != 0) return luaL_error(L, "splice failed");
            return 0;
        }
        return 0;
    }

    if (strcmp(ns, "prompts") == 0) {
        if (lua_isnil(L, 3)) {
            system_prompt_remove_fragment(key);
        } else {
            const char *text = luaL_checkstring(L, 3);
            system_prompt_add_fragment(key, text);
        }
        return 0;
    }

    if (strcmp(ns, "config") == 0) {
        return luaL_error(L, "config.%s is read-only from extensions", key);
    }

    if (strcmp(ns, "settings") == 0) {
        if (api && api->settings) {
            cJSON *old = cJSON_DetachItemFromObject(api->settings, key);
            if (old) cJSON_Delete(old);
            if (!lua_isnil(L, 3)) cJSON_AddItemToObject(api->settings, key, lua_to_cjson(L, 3));
        }
        return 0;
    }

    if (strcmp(ns, "state") == 0) {
        if (api && api->state) {
            cJSON *old = cJSON_DetachItemFromObject(api->state, key);
            if (old) cJSON_Delete(old);
            if (!lua_isnil(L, 3)) cJSON_AddItemToObject(api->state, key, lua_to_cjson(L, 3));
        }
        return 0;
    }

    return luaL_error(L, "unknown namespace: %s", ns);
}

/* ============================================================
 *  JSON utilities
 * ============================================================ */

static int lua_json_encode(lua_State *L) {
    cJSON *j = lua_to_cjson(L, 1);
    char *s = cJSON_PrintUnformatted(j);
    lua_pushstring(L, s ? s : "null");
    free(s);
    cJSON_Delete(j);
    return 1;
}

static int lua_json_decode(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    cJSON *json = cJSON_Parse(str);
    if (!json) { lua_pushnil(L); return 1; }
    push_cjson(L, json);
    cJSON_Delete(json);
    return 1;
}

/* ============================================================
 *  Register the rig global
 * ============================================================ */

static const struct luaL_Reg rig_methods[] = {
    {"exec",       lua_rig_exec},
    {"completion", lua_rig_completion},
    {"print",      lua_rig_print},
    {"input",      lua_rig_input},
    {"hook",       lua_rig_hook},
    {"unhook",     lua_rig_unhook},
    {"get",        lua_rig_get},
    {"set",        lua_rig_set},
    {NULL, NULL},
};

static void register_rig(lua_State *L, RigExtensionAPI *api, LuaExtState *state) {
    lua_pushlightuserdata(L, api);
    lua_setfield(L, LUA_REGISTRYINDEX, REG_API_KEY);
    lua_pushlightuserdata(L, state);
    lua_setfield(L, LUA_REGISTRYINDEX, REG_STATE_KEY);

    lua_newtable(L);
    for (const struct luaL_Reg *m = rig_methods; m->name; m++) {
        lua_pushcfunction(L, m->func);
        lua_setfield(L, -2, m->name);
    }
    lua_pushvalue(L, -1);
    /* "pi" global removed — use "rig" */
    lua_setglobal(L, "rig");
}

static void register_json(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_json_encode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, lua_json_decode);
    lua_setfield(L, -2, "decode");
    lua_setglobal(L, "json");
}

/* ============================================================
 *  Public API
 * ============================================================ */

LuaExtState *lua_ext_create(RigExtensionAPI *api) {
    LuaExtState *state = calloc(1, sizeof(LuaExtState));
    if (!state) return NULL;

    state->api = api;
    state->sandboxed = true;
    pthread_mutex_init(&state->lua_mutex, NULL);
    state->L = lua_newstate(lua_alloc_tracked, state);
    if (!state->L) { pthread_mutex_destroy(&state->lua_mutex); free(state); return NULL; }

    luaL_openlibs(state->L);
    if (state->sandboxed) sandbox_lua(state->L);
    register_json(state->L);
    register_rig(state->L, api, state);

    return state;
}

void lua_ext_free(LuaExtState *state) {
    if (!state) return;
    for (int i = 0; i < state->alloc_count; i++) free(state->allocs[i]);
    free(state->allocs);
    if (state->L) lua_close(state->L);
    pthread_mutex_destroy(&state->lua_mutex);
    free(state);
}

void lua_ext_set_context(LuaExtState *state, RigLuaContext *ctx) {
    if (!state || !state->L) return;
    state->ctx = ctx;
    lua_pushlightuserdata(state->L, ctx);
    lua_setfield(state->L, LUA_REGISTRYINDEX, REG_CTX_KEY);
}

int lua_ext_load_file(LuaExtState *state, const char *path) {
    if (!state || !path || !state->L) return -1;
    pthread_mutex_lock(&state->lua_mutex);

    lua_sethook(state->L, lua_hook_count, LUA_MASKCOUNT, LUA_INIT_INSTRUCTION_LIMIT);

    if (luaL_loadfile(state->L, path) != LUA_OK) {
        LOG_ERROR("[lua] load error: %s", lua_tostring(state->L, -1));
        lua_pop(state->L, 1);
        lua_sethook(state->L, NULL, 0, 0);
        pthread_mutex_unlock(&state->lua_mutex);
        return -1;
    }

    if (lua_pcall(state->L, 0, 0, 0) != LUA_OK) {
        LOG_ERROR("[lua] exec error: %s", lua_tostring(state->L, -1));
        lua_pop(state->L, 1);
        lua_sethook(state->L, NULL, 0, 0);
        pthread_mutex_unlock(&state->lua_mutex);
        return -1;
    }

    lua_sethook(state->L, NULL, 0, 0);
    state->loaded = true;
    pthread_mutex_unlock(&state->lua_mutex);
    return 0;
}

int lua_ext_call_init(LuaExtState *state) {
    if (!state || !state->L) return -1;
    pthread_mutex_lock(&state->lua_mutex);

    lua_sethook(state->L, lua_hook_count, LUA_MASKCOUNT, LUA_INIT_INSTRUCTION_LIMIT);

    lua_getglobal(state->L, "init");
    if (!lua_isfunction(state->L, -1)) {
        lua_pop(state->L, 1);
        lua_sethook(state->L, NULL, 0, 0);
        pthread_mutex_unlock(&state->lua_mutex);
        return 0;
    }

    lua_getglobal(state->L, "rig");
    if (lua_pcall(state->L, 1, 0, 0) != LUA_OK) {
        LOG_ERROR("[lua] init error: %s", lua_tostring(state->L, -1));
        lua_pop(state->L, 1);
        lua_sethook(state->L, NULL, 0, 0);
        pthread_mutex_unlock(&state->lua_mutex);
        return -1;
    }

    lua_sethook(state->L, NULL, 0, 0);
    pthread_mutex_unlock(&state->lua_mutex);
    return 0;
}

int lua_ext_eval(LuaExtState *state, const char *code, char **result) {
    if (!state || !code || !state->L) return -1;
    pthread_mutex_lock(&state->lua_mutex);

    lua_sethook(state->L, lua_hook_count, LUA_MASKCOUNT, LUA_INSTRUCTION_LIMIT);

    if (luaL_dostring(state->L, code) != LUA_OK) {
        if (result) *result = strdup(lua_tostring(state->L, -1));
        lua_pop(state->L, 1);
        lua_sethook(state->L, NULL, 0, 0);
        pthread_mutex_unlock(&state->lua_mutex);
        return -1;
    }

    if (result && lua_gettop(state->L) > 0) {
        const char *r = lua_tostring(state->L, -1);
        *result = r ? strdup(r) : NULL;
        lua_pop(state->L, 1);
    }

    lua_sethook(state->L, NULL, 0, 0);
    pthread_mutex_unlock(&state->lua_mutex);
    return 0;
}

int lua_ext_set_var(LuaExtState *state, const char *name, const char *value) {
    if (!state || !name || !state->L) return -1;
    pthread_mutex_lock(&state->lua_mutex);
    if (value) lua_pushstring(state->L, value);
    else lua_pushnil(state->L);
    lua_setglobal(state->L, name);
    pthread_mutex_unlock(&state->lua_mutex);
    return 0;
}

int lua_ext_set_var_json(LuaExtState *state, const char *name, cJSON *value) {
    if (!state || !name || !state->L) return -1;
    pthread_mutex_lock(&state->lua_mutex);
    push_cjson(state->L, value);
    lua_setglobal(state->L, name);
    pthread_mutex_unlock(&state->lua_mutex);
    return 0;
}

char *lua_ext_get_var(LuaExtState *state, const char *name) {
    if (!state || !name || !state->L) return NULL;
    pthread_mutex_lock(&state->lua_mutex);
    lua_getglobal(state->L, name);
    const char *r = lua_tostring(state->L, -1);
    char *result = r ? strdup(r) : NULL;
    lua_pop(state->L, 1);
    pthread_mutex_unlock(&state->lua_mutex);
    return result;
}

bool lua_ext_is_loaded(LuaExtState *state) {
    return state && state->loaded;
}

int lua_ext_call_tool(LuaExtState *state, const char *name,
                      cJSON *params, char **result) {
    if (!state || !name || !state->L) return -1;
    pthread_mutex_lock(&state->lua_mutex);

    lua_getfield(state->L, LUA_REGISTRYINDEX, REG_TOOLS_KEY);
    if (lua_isnil(state->L, -1)) { lua_pop(state->L, 1); pthread_mutex_unlock(&state->lua_mutex); return -1; }
    lua_getfield(state->L, -1, name);
    if (lua_isnil(state->L, -1)) { lua_pop(state->L, 2); pthread_mutex_unlock(&state->lua_mutex); return -1; }
    int ref = (int)lua_tointeger(state->L, -1);
    lua_pop(state->L, 2);

    lua_rawgeti(state->L, LUA_REGISTRYINDEX, ref);
    push_cjson(state->L, params);

    int rc = 0;
    if (lua_pcall(state->L, 1, 1, 0) != LUA_OK) {
        LOG_ERROR("[lua] tool error: %s", lua_tostring(state->L, -1));
        if (result) *result = strdup(lua_tostring(state->L, -1));
        lua_pop(state->L, 1);
        rc = -1;
    } else {
        if (result) {
            const char *r = lua_tostring(state->L, -1);
            *result = r ? strdup(r) : NULL;
        }
        lua_pop(state->L, 1);
    }

    pthread_mutex_unlock(&state->lua_mutex);
    return rc;
}
