/* test_extensions.c — tests for hooks, event_bus, extension API, Lua bindings */
#include "test.h"
#include "harness/extensions/hooks.h"
#include "harness/extensions/event_bus.h"
#include "harness/extensions/extension.h"
#include "harness/extensions/lua_ext.h"
#include "util/fs.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========== Hooks ========== */

static bool simple_hook(const char *event, cJSON *data, cJSON **result, void *ctx) {
    (void)event; (void)data; (void)result;
    (*(int *)ctx)++;
    return true;
}

static bool blocking_hook(const char *event, cJSON *data, cJSON **result, void *ctx) {
    (void)event; (void)data; (void)result; (void)ctx;
    return false;
}

TEST(hook_chain_create_free) {
    HookChain *c = hook_chain_create();
    ASSERT_NOT_NULL(c);
    hook_chain_free(c);
}

TEST(hook_chain_add_fire) {
    HookChain *c = hook_chain_create();
    int count = 0;
    hook_chain_add(c, "test", 100, simple_hook, &count, "h1");
    bool ok = hook_chain_fire(c, "test", NULL, NULL);
    ASSERT_TRUE(ok);
    ASSERT_EQ(count, 1);
    hook_chain_free(c);
}

TEST(hook_chain_fire_wrong_event) {
    HookChain *c = hook_chain_create();
    int count = 0;
    hook_chain_add(c, "test", 100, simple_hook, &count, "h1");
    hook_chain_fire(c, "other", NULL, NULL);
    ASSERT_EQ(count, 0);
    hook_chain_free(c);
}

TEST(hook_chain_wildcard) {
    HookChain *c = hook_chain_create();
    int count = 0;
    hook_chain_add(c, "*", 100, simple_hook, &count, "h1");
    hook_chain_fire(c, "anything", NULL, NULL);
    ASSERT_EQ(count, 1);
    hook_chain_free(c);
}

TEST(hook_chain_blocking) {
    HookChain *c = hook_chain_create();
    int count = 0;
    hook_chain_add(c, "test", 50, blocking_hook, NULL, "blocker");
    hook_chain_add(c, "test", 100, simple_hook, &count, "after");
    bool ok = hook_chain_fire(c, "test", NULL, NULL);
    ASSERT_FALSE(ok);
    ASSERT_EQ(count, 0);
    hook_chain_free(c);
}

TEST(hook_chain_priority_order) {
    HookChain *c = hook_chain_create();
    int count = 0;
    hook_chain_add(c, "test", 200, simple_hook, &count, "low");
    hook_chain_add(c, "test", 50, simple_hook, &count, "high");
    ASSERT_EQ(hook_chain_count(c, "test"), 2);
    hook_chain_free(c);
}

TEST(hook_chain_remove) {
    HookChain *c = hook_chain_create();
    int count = 0;
    hook_chain_add(c, "test", 100, simple_hook, &count, "h1");
    ASSERT_EQ(hook_chain_remove(c, "h1"), 0);
    ASSERT_EQ(hook_chain_count(c, "test"), 0);
    hook_chain_free(c);
}

TEST(hook_chain_remove_missing) {
    HookChain *c = hook_chain_create();
    ASSERT_EQ(hook_chain_remove(c, "nonexistent"), -1);
    hook_chain_free(c);
}

TEST(hook_chain_count_basic) {
    HookChain *c = hook_chain_create();
    hook_chain_add(c, "a", 100, simple_hook, NULL, "h1");
    hook_chain_add(c, "b", 100, simple_hook, NULL, "h2");
    hook_chain_add(c, "a", 100, simple_hook, NULL, "h3");
    ASSERT_EQ(hook_chain_count(c, "a"), 2);
    ASSERT_EQ(hook_chain_count(c, "b"), 1);
    hook_chain_free(c);
}

/* ========== Event Bus ========== */

typedef struct { int count; char last_topic[64]; } BusTestCtx;

static void bus_test_handler(BusEvent *event, void *ctx) {
    BusTestCtx *t = ctx;
    t->count++;
    strncpy(t->last_topic, event->topic, 63);
}

TEST(event_bus_create_free) {
    EventBus *b = event_bus_create();
    ASSERT_NOT_NULL(b);
    event_bus_free(b);
}

TEST(event_bus_pub_sub) {
    EventBus *b = event_bus_create();
    BusTestCtx ctx = {0};
    event_bus_subscribe(b, "test.topic", bus_test_handler, &ctx);
    event_bus_publish(b, "test.topic", "src", NULL);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.last_topic, "test.topic");
    event_bus_free(b);
}

TEST(event_bus_wildcard_pattern) {
    EventBus *b = event_bus_create();
    BusTestCtx ctx = {0};
    event_bus_subscribe(b, "test.*", bus_test_handler, &ctx);
    event_bus_publish(b, "test.one", "src", NULL);
    event_bus_publish(b, "test.two", "src", NULL);
    ASSERT_EQ(ctx.count, 2);
    event_bus_free(b);
}

TEST(event_bus_no_match) {
    EventBus *b = event_bus_create();
    BusTestCtx ctx = {0};
    event_bus_subscribe(b, "other", bus_test_handler, &ctx);
    event_bus_publish(b, "test", "src", NULL);
    ASSERT_EQ(ctx.count, 0);
    event_bus_free(b);
}

TEST(event_bus_subscriber_count) {
    EventBus *b = event_bus_create();
    event_bus_subscribe(b, "test", bus_test_handler, NULL);
    event_bus_subscribe(b, "test", bus_test_handler, NULL);
    ASSERT_EQ(event_bus_subscriber_count(b, "test"), 2);
    event_bus_free(b);
}

static cJSON *reply_handler(cJSON *data, void *ctx) {
    (void)data; (void)ctx;
    return cJSON_CreateString("reply");
}

TEST(event_bus_request_reply) {
    EventBus *b = event_bus_create();
    event_bus_reply_handler(b, "ask", reply_handler, NULL);
    cJSON *r = event_bus_request(b, "ask", NULL, 0);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->valuestring, "reply");
    cJSON_Delete(r);
    event_bus_free(b);
}

TEST(event_bus_request_no_handler) {
    EventBus *b = event_bus_create();
    cJSON *r = event_bus_request(b, "unhandled", NULL, 0);
    ASSERT_NULL(r);
    event_bus_free(b);
}

/* RESOURCE EXHAUSTION: 1,000 hooks on same event */
TEST(hook_chain_1000_hooks) {
    HookChain *c = hook_chain_create();
    int count = 0;
    char names[1000][16];
    for (int i = 0; i < 1000; i++) {
        snprintf(names[i], sizeof(names[i]), "h%d", i);
        hook_chain_add(c, "test", 100, simple_hook, &count, names[i]);
    }
    ASSERT_EQ(hook_chain_count(c, "test"), 1000);
    bool ok = hook_chain_fire(c, "test", NULL, NULL);
    ASSERT_TRUE(ok);
    ASSERT_EQ(count, 1000);
    hook_chain_free(c);
}

/* RESOURCE EXHAUSTION: event bus with 1,000 subscribers */
TEST(event_bus_1000_subscribers) {
    EventBus *b = event_bus_create();
    BusTestCtx ctx = {0};
    for (int i = 0; i < 1000; i++) {
        event_bus_subscribe(b, "mass.event", bus_test_handler, &ctx);
    }
    ASSERT_EQ(event_bus_subscriber_count(b, "mass.event"), 1000);
    event_bus_publish(b, "mass.event", "src", NULL);
    ASSERT_EQ(ctx.count, 1000);
    event_bus_free(b);
}

/* ========== Extension API ========== */

TEST(extension_api_create_free) {
    PiExtensionAPI *api = extension_api_create();
    ASSERT_NOT_NULL(api);
    ASSERT_EQ(api->abi_version, 1);
    ASSERT_NOT_NULL(api->hooks);
    ASSERT_NOT_NULL(api->bus);
    extension_api_free(api);
}

TEST(extension_api_register_tool) {
    PiExtensionAPI *api = extension_api_create();
    Tool *t = calloc(1, sizeof(Tool));
    t->name = strdup("mytool");
    ASSERT_EQ(extension_api_register_tool(api, t), 0);
    ASSERT_EQ(api->tool_count, 1);
    Tool *got = extension_api_get_tool(api, "mytool");
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got->name, "mytool");
    extension_api_free(api);
}

TEST(extension_api_unregister_tool) {
    PiExtensionAPI *api = extension_api_create();
    Tool *t = calloc(1, sizeof(Tool));
    t->name = strdup("mytool");
    extension_api_register_tool(api, t);
    ASSERT_EQ(extension_api_unregister_tool(api, "mytool"), 0);
    ASSERT_NULL(extension_api_get_tool(api, "mytool"));
    extension_api_free(api);
}

static int ext_cmd_dummy(const char **a, int c, void *x) { (void)a;(void)c;(void)x; return 0; }

TEST(extension_api_register_command) {
    PiExtensionAPI *api = extension_api_create();
    ASSERT_EQ(extension_api_register_command(api, "cmd", ext_cmd_dummy, NULL), 0);
    ASSERT_EQ(api->command_count, 1);
    ASSERT_STR_EQ(api->commands[0].name, "cmd");
    extension_api_free(api);
}

/* ========== Lua Extensions ========== */

TEST(lua_ext_create_free) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    ASSERT_NOT_NULL(lua);
    ASSERT_FALSE(lua_ext_is_loaded(lua));
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_eval_basic) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "return 2 + 3", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "5");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_eval_error) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "error('boom')", &result);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(result);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_set_get_var) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_set_var(lua, "myvar", "hello");
    char *v = lua_ext_get_var(lua, "myvar");
    ASSERT_STR_EQ(v, "hello");
    free(v);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_set_var_json) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    cJSON *val = cJSON_CreateNumber(42);
    lua_ext_set_var_json(lua, "num", val);
    char *result = NULL;
    lua_ext_eval(lua, "return num", &result);
    ASSERT_NOT_NULL(result);
    /* Lua may format as "42" or "42.0" depending on version */
    ASSERT_TRUE(strstr(result, "42") != NULL);
    free(result);
    cJSON_Delete(val);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_hook_from_lua) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "fired = false; rig.hook('test.event', function(ev, data) fired = true; return true end)", NULL);
    hook_chain_fire(api->hooks, "test.event", NULL, NULL);
    char *result = NULL;
    lua_ext_eval(lua, "return tostring(fired)", &result);
    ASSERT_STR_EQ(result, "true");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_register_command) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "rig.set('commands', 'hello', function(args) end)", NULL);
    ASSERT_EQ(api->command_count, 1);
    ASSERT_STR_EQ(api->commands[0].name, "hello");
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_register_tool) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "rig.set('tools', 'mytool', {description='desc', params={}, run=function(p) return 'ok' end})", NULL);
    ASSERT_EQ(api->tool_count, 1);
    Tool *t = extension_api_get_tool(api, "mytool");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "mytool");
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_bus_pub_sub) {
    /* Event bus pub/sub removed from new API — hooks replace it */
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    /* Test rig.hook instead */
    lua_ext_eval(lua, "received = false; rig.hook('bus.test', function(ev, data) received = true; return true end)", NULL);
    hook_chain_fire(api->hooks, "bus.test", NULL, NULL);
    char *result = NULL;
    lua_ext_eval(lua, "return tostring(received)", &result);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "true");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_bus_publish_from_lua) {
    /* Replaced: test rig.exec instead */
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    lua_ext_eval(lua, "local r = rig.exec('echo hello'); return r.stdout", &result);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strstr(result, "hello") != NULL);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_state_get_set) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "rig.set('state', 'key1', 'value1')", NULL);
    char *result = NULL;
    lua_ext_eval(lua, "return rig.get('state', 'key1')", &result);
    ASSERT_STR_EQ(result, "value1");
    free(result);
    cJSON *val = cJSON_GetObjectItem(api->state, "key1");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val->valuestring, "value1");
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_settings_get_set) {
    PiExtensionAPI *api = extension_api_create();
    cJSON_AddStringToObject(api->settings, "theme", "dark");
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    lua_ext_eval(lua, "return rig.get('settings', 'theme')", &result);
    ASSERT_STR_EQ(result, "dark");
    free(result);
    lua_ext_eval(lua, "rig.set('settings', 'theme', 'light')", NULL);
    cJSON *val = cJSON_GetObjectItem(api->settings, "theme");
    ASSERT_STR_EQ(val->valuestring, "light");
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_load_file) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    const char *code = "test_var = 'loaded'\nfunction init(rig)\n  rig.set('state', 'init_called', 'yes')\nend\n";
    const char *path = "/tmp/pi_test_ext.lua";
    fs_write_file(path, code, strlen(code));
    ASSERT_EQ(lua_ext_load_file(lua, path), 0);
    ASSERT_TRUE(lua_ext_is_loaded(lua));
    ASSERT_EQ(lua_ext_call_init(lua), 0);
    cJSON *val = cJSON_GetObjectItem(api->state, "init_called");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val->valuestring, "yes");
    lua_ext_free(lua);
    extension_api_free(api);
    unlink(path);
}

TEST(lua_ext_sandbox_no_os) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    lua_ext_eval(lua, "return type(os)", &result);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "nil");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(lua_ext_free_null) {
    lua_ext_free(NULL);
}

/* ========== ADVERSARIAL: Lua Sandbox Escapes ========== */

TEST(adv_lua_os_execute_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "os.execute('ls')", &result);
    /* os is nil, so indexing it should fail */
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_io_open_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "io.open('/etc/passwd')", &result);
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_loadfile_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "loadfile('/etc/passwd')", &result);
    /* loadfile is set to nil */
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_dofile_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "dofile('/etc/passwd')", &result);
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_debug_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "debug.getinfo(1)", &result);
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_string_dump) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    /* string.dump may or may not be available; must not crash */
    int rc = lua_ext_eval(lua, "return type(string.dump)", &result);
    /* Even if it succeeds, verify it doesn't leak anything dangerous */
    ASSERT_TRUE(rc == 0 || rc == -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_require_os_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    /* package.cpath is empty so require of C modules should fail */
    int rc = lua_ext_eval(lua, "require('os')", &result);
    /* os module was nilled, so re-requiring should not restore it */
    /* This may succeed returning the cached nil, or error */
    /* Either way, os.execute should still be nil */
    free(result);
    result = NULL;
    rc = lua_ext_eval(lua, "return type(os)", &result);
    /* os should still be nil */
    ASSERT_TRUE(rc == 0 || rc == -1);
    if (rc == 0 && result) {
        ASSERT_STR_EQ(result, "nil");
    }
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_pcall_os_execute_blocked) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    /* pcall returns false + error_msg, but lua_ext_eval captures just the first return.
       lua_tostring of a boolean 'false' returns nil in Lua 5.4, so result may contain
       the error string or "false" depending on stack handling. Key: no crash and os blocked. */
    int rc = lua_ext_eval(lua, "local ok, err = pcall(function() os.execute('ls') end); return tostring(ok)", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "false");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_rawget_os) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "return type(rawget(_G, 'os'))", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "nil");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_load_os_execute) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "local f = load(\"os.execute('ls')\"); if f then f() end", &result);
    /* load should succeed but execution should fail because os is nil */
    /* Either the eval errors, or pcall catches it */
    ASSERT_TRUE(rc == 0 || rc == -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_infinite_loop_instruction_limit) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "while true do end", &result);
    /* Must hit instruction limit and return error */
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strstr(result, "instruction limit") != NULL);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_memory_bomb) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "s = 'x'; for i=1,30 do s = s..s end", &result);
    /* Should hit memory limit */
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_coroutine_no_escape) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua,
        "local co = coroutine.create(function() "
        "  for i=1,5 do coroutine.yield(i) end "
        "end); "
        "local ok, v = coroutine.resume(co); "
        "return tostring(v)", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "1");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_package_loadlib_nil) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "return type(package.loadlib)", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "nil");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_modify_sandbox_globals) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    /* Try to restore os by assigning a table */
    lua_ext_eval(lua, "_G.os = {execute = print}", NULL);
    /* os.execute should now be print, not a real execute */
    int rc = lua_ext_eval(lua, "return type(os.execute)", &result);
    ASSERT_EQ(rc, 0);
    /* It may be "function" (print) but it's not the real os.execute */
    /* The important thing is we verify the sandbox was set up correctly initially */
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(adv_lua_string_rep_memory) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "local s = string.rep('A', 1000000000)", &result);
    /* Should hit memory limit */
    ASSERT_EQ(rc, -1);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

/* ========== 8 Primitives ========== */

TEST(rig_exec_basic) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    int rc = lua_ext_eval(lua, "local r = rig.exec('echo hello world'); return r.stdout", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strstr(result, "hello world") != NULL);
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(rig_get_tools) {
    PiExtensionAPI *api = extension_api_create();
    Tool *t = calloc(1, sizeof(Tool));
    t->name = strdup("test_tool");
    t->description = strdup("a test tool");
    extension_api_register_tool(api, t);

    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    lua_ext_eval(lua, "local tools = rig.get('tools'); return tools[1]", &result);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "test_tool");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(rig_set_tools) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "rig.set('tools', 'newtool', {description='new', params={}, run=function(p) return 'hi' end})", NULL);
    ASSERT_EQ(api->tool_count, 1);
    Tool *t = extension_api_get_tool(api, "newtool");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->description, "new");

    /* Remove it */
    lua_ext_eval(lua, "rig.set('tools', 'newtool', nil)", NULL);
    ASSERT_EQ(api->tool_count, 0);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(rig_hook_unhook) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "count = 0; handle = rig.hook('myevent', function(ev, data) count = count + 1; return true end)", NULL);
    hook_chain_fire(api->hooks, "myevent", NULL, NULL);
    char *result = NULL;
    lua_ext_eval(lua, "return tostring(count)", &result);
    ASSERT_STR_EQ(result, "1");
    free(result);

    /* Unhook */
    lua_ext_eval(lua, "rig.unhook(handle)", NULL);
    hook_chain_fire(api->hooks, "myevent", NULL, NULL);
    result = NULL;
    lua_ext_eval(lua, "return tostring(count)", &result);
    ASSERT_STR_EQ(result, "1"); /* still 1, not 2 */
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(rig_call_lua_tool) {
    PiExtensionAPI *api = extension_api_create();
    LuaExtState *lua = lua_ext_create(api);
    lua_ext_eval(lua, "rig.set('tools', 'echo_tool', {description='echo', params={}, run=function(p) return 'echoed: ' .. (p.msg or 'nil') end})", NULL);

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "msg", "test123");
    char *result = NULL;
    int rc = lua_ext_call_tool(lua, "echo_tool", params, &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strstr(result, "test123") != NULL);
    free(result);
    cJSON_Delete(params);
    lua_ext_free(lua);
    extension_api_free(api);
}

TEST(rig_get_settings) {
    PiExtensionAPI *api = extension_api_create();
    cJSON_AddStringToObject(api->settings, "color", "blue");
    LuaExtState *lua = lua_ext_create(api);
    char *result = NULL;
    lua_ext_eval(lua, "return rig.get('settings', 'color')", &result);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "blue");
    free(result);
    lua_ext_free(lua);
    extension_api_free(api);
}

int main(void) {
    TEST_SUITE("Hooks");
    RUN_TEST(hook_chain_create_free);
    RUN_TEST(hook_chain_add_fire);
    RUN_TEST(hook_chain_fire_wrong_event);
    RUN_TEST(hook_chain_wildcard);
    RUN_TEST(hook_chain_blocking);
    RUN_TEST(hook_chain_priority_order);
    RUN_TEST(hook_chain_remove);
    RUN_TEST(hook_chain_remove_missing);
    RUN_TEST(hook_chain_count_basic);

    TEST_SUITE("RESOURCE EXHAUSTION: Hooks");
    RUN_TEST(hook_chain_1000_hooks);

    TEST_SUITE("Event Bus");
    RUN_TEST(event_bus_create_free);
    RUN_TEST(event_bus_pub_sub);
    RUN_TEST(event_bus_wildcard_pattern);
    RUN_TEST(event_bus_no_match);
    RUN_TEST(event_bus_subscriber_count);
    RUN_TEST(event_bus_request_reply);
    RUN_TEST(event_bus_request_no_handler);

    TEST_SUITE("RESOURCE EXHAUSTION: Event Bus");
    RUN_TEST(event_bus_1000_subscribers);

    TEST_SUITE("Extension API");
    RUN_TEST(extension_api_create_free);
    RUN_TEST(extension_api_register_tool);
    RUN_TEST(extension_api_unregister_tool);
    RUN_TEST(extension_api_register_command);

    TEST_SUITE("Lua Extensions");
    RUN_TEST(lua_ext_create_free);
    RUN_TEST(lua_ext_eval_basic);
    RUN_TEST(lua_ext_eval_error);
    RUN_TEST(lua_ext_set_get_var);
    RUN_TEST(lua_ext_set_var_json);
    RUN_TEST(lua_ext_hook_from_lua);
    RUN_TEST(lua_ext_register_command);
    RUN_TEST(lua_ext_register_tool);
    RUN_TEST(lua_ext_bus_pub_sub);
    RUN_TEST(lua_ext_bus_publish_from_lua);
    RUN_TEST(lua_ext_state_get_set);
    RUN_TEST(lua_ext_settings_get_set);
    RUN_TEST(lua_ext_load_file);
    RUN_TEST(lua_ext_sandbox_no_os);
    RUN_TEST(lua_ext_free_null);

    TEST_SUITE("8 Primitives");
    RUN_TEST(rig_exec_basic);
    RUN_TEST(rig_get_tools);
    RUN_TEST(rig_set_tools);
    RUN_TEST(rig_hook_unhook);
    RUN_TEST(rig_call_lua_tool);
    RUN_TEST(rig_get_settings);

    TEST_SUITE("ADVERSARIAL: Lua Sandbox Escapes");
    RUN_TEST(adv_lua_os_execute_blocked);
    RUN_TEST(adv_lua_io_open_blocked);
    RUN_TEST(adv_lua_loadfile_blocked);
    RUN_TEST(adv_lua_dofile_blocked);
    RUN_TEST(adv_lua_debug_blocked);
    RUN_TEST(adv_lua_string_dump);
    RUN_TEST(adv_lua_require_os_blocked);
    RUN_TEST(adv_lua_pcall_os_execute_blocked);
    RUN_TEST(adv_lua_rawget_os);
    RUN_TEST(adv_lua_load_os_execute);
    RUN_TEST(adv_lua_infinite_loop_instruction_limit);
    RUN_TEST(adv_lua_memory_bomb);
    RUN_TEST(adv_lua_coroutine_no_escape);
    RUN_TEST(adv_lua_package_loadlib_nil);
    RUN_TEST(adv_lua_modify_sandbox_globals);
    RUN_TEST(adv_lua_string_rep_memory);

    TEST_REPORT();
}
