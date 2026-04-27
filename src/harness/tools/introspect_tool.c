#include "tools.h"
#include "harness/config.h"
#include "harness/permissions.h"
#include "harness/extensions/extension.h"
#include "util/str.h"
#include "util/fs.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static PiExtensionAPI *introspect_api = NULL;
static PermissionSet *introspect_perms = NULL;
static const Tool *introspect_tools = NULL;
static int introspect_tool_count = 0;
static const char *introspect_cwd = NULL;

void introspect_tool_set_context(PiExtensionAPI *api, PermissionSet *perms,
                                  const Tool *tools, int tool_count,
                                  const char *cwd) {
    introspect_api = api;
    introspect_perms = perms;
    introspect_tools = tools;
    introspect_tool_count = tool_count;
    introspect_cwd = cwd;
}

static int introspect_execute(const char *call_id, cJSON *params, void *signal,
                               void (*on_update)(void *ctx, cJSON *partial), void *ctx,
                               ContentBlock **content, int *content_count,
                               cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)ctx;
    (void)details; (void)terminate;

    cJSON *query_j = cJSON_GetObjectItem(params, "query");
    const char *query = (query_j && cJSON_IsString(query_j)) ? query_j->valuestring : "help";

    Str out = str_new(2048);

    if (strcmp(query, "tools") == 0) {
        str_append(&out, "Available tools:\n");
        for (int i = 0; i < introspect_tool_count; i++) {
            str_appendf(&out, "- %s: %s\n",
                        introspect_tools[i].name,
                        introspect_tools[i].description ? introspect_tools[i].description : "");
        }
    } else if (strcmp(query, "extensions") == 0) {
        if (introspect_api && introspect_api->extension_count > 0) {
            str_append(&out, "Loaded extensions:\n");
            for (int i = 0; i < introspect_api->extension_count; i++) {
                Extension *ext = introspect_api->extensions[i];
                if (ext) {
                    str_appendf(&out, "- %s (%s) %s\n",
                                ext->name ? ext->name : "?",
                                ext->is_lua ? "lua" : ext->is_yaml ? "yaml" : "native",
                                ext->path ? ext->path : "");
                }
            }
        } else {
            str_append(&out, "No extensions loaded.\n");
        }
    } else if (strcmp(query, "trust") == 0) {
        if (introspect_perms) {
            if (introspect_perms->yolo) {
                str_append(&out, "Trust mode: ALL (yolo)\n");
            } else if (introspect_perms->count == 0) {
                str_append(&out, "Trust: none — all tools prompt for permission\n");
            } else {
                str_append(&out, "Trust rules:\n");
                for (int i = 0; i < introspect_perms->count; i++) {
                    TrustRule *r = &introspect_perms->rules[i];
                    if (r->pattern) {
                        str_appendf(&out, "- %s '%s'\n", r->tool, r->pattern);
                    } else {
                        str_appendf(&out, "- %s (all)\n", r->tool);
                    }
                }
            }
        }
    } else if (strcmp(query, "config") == 0) {
        str_append(&out, "Configuration:\n");
        if (introspect_cwd) str_appendf(&out, "- cwd: %s\n", introspect_cwd);
        str_appendf(&out, "- project_dir: %s\n", config_project_dir());
        str_appendf(&out, "- agent_dir: %s\n", config_agent_dir());
        str_appendf(&out, "- sessions_dir: %s\n", config_sessions_dir());
    } else if (strcmp(query, "commands") == 0) {
        str_append(&out, "Slash commands:\n");
        str_append(&out, "- /help, /model, /tools, /run, /find, /diff\n");
        str_append(&out, "- /undo, /context, /session, /sessions, /fork\n");
        str_append(&out, "- /theme, /trust, /ext, /clear, /exit\n");
        if (introspect_api) {
            for (int i = 0; i < introspect_api->command_count; i++) {
                str_appendf(&out, "- /%s (extension)\n", introspect_api->commands[i].name);
            }
        }
    } else if (strcmp(query, "project") == 0) {
        const char *pd = config_project_dir();
        str_appendf(&out, "Project directory: %s\n", pd ? pd : "none");
        if (pd && fs_is_dir(pd)) {
            str_append(&out, "Contents:\n");
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "ls -1 '%s' 2>/dev/null", pd);
            FILE *p = popen(cmd, "r");
            if (p) {
                char line[256];
                while (fgets(line, sizeof(line), p)) {
                    str_appendf(&out, "  %s", line);
                }
                pclose(p);
            }
        } else {
            str_append(&out, "No .rig directory found.\n");
        }
    } else {
        str_append(&out, "Rig introspection tool — query the agent's own state.\n\n");
        str_append(&out, "Available queries:\n");
        str_append(&out, "- tools      — list all available tools with descriptions\n");
        str_append(&out, "- extensions — list loaded Lua/YAML/native extensions\n");
        str_append(&out, "- trust      — show current permission/trust rules\n");
        str_append(&out, "- config     — show directories and configuration\n");
        str_append(&out, "- commands   — list all slash commands (built-in + extension)\n");
        str_append(&out, "- project    — show .rig project directory contents\n");
        str_append(&out, "- help       — show this help\n");
    }

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(out.data ? out.data : "", NULL);
    *content_count = 1;
    str_free(&out);
    return 0;
}

Tool tool_introspect_create(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "type", "string");
    cJSON_AddStringToObject(q, "description",
        "What to query. One of: tools, extensions, trust, config, commands, project, help");
    cJSON *qenum = cJSON_CreateArray();
    cJSON_AddItemToArray(qenum, cJSON_CreateString("tools"));
    cJSON_AddItemToArray(qenum, cJSON_CreateString("extensions"));
    cJSON_AddItemToArray(qenum, cJSON_CreateString("trust"));
    cJSON_AddItemToArray(qenum, cJSON_CreateString("config"));
    cJSON_AddItemToArray(qenum, cJSON_CreateString("commands"));
    cJSON_AddItemToArray(qenum, cJSON_CreateString("project"));
    cJSON_AddItemToArray(qenum, cJSON_CreateString("help"));
    cJSON_AddItemToObject(q, "enum", qenum);
    cJSON_AddItemToObject(props, "query", q);
    cJSON_AddItemToObject(params, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("query"));
    cJSON_AddItemToObject(params, "required", req);

    return (Tool){
        .name = "introspect",
        .description = "Query Rig's own state: tools, extensions, trust rules, config, commands, project info",
        .parameters = params,
        .execute = introspect_execute,
    };
}
