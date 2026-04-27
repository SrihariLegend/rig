#include "tools.h"
#include "util/str.h"
#include "util/process.h"
#include "util/json.h"
#include <stdlib.h>
#include <string.h>

static void collect(const char *data, size_t len, void *ctx) {
    str_append_len(ctx, data, len);
}

static int grep_execute(const char *call_id, cJSON *params, void *signal,
                        void (*on_update)(void *ctx, cJSON *partial), void *update_ctx,
                        ContentBlock **content, int *content_count,
                        cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)update_ctx; (void)terminate;

    const char *pattern = json_get_string(params, "pattern");
    const char *path = json_get_string(params, "path");

    if (!pattern) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: pattern is required", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    Str cmd = str_new(512);
    str_append(&cmd, "grep -rn --color=never");
    if (json_get_bool(params, "ignore_case", false)) str_append(&cmd, " -i");
    int ctx_lines = json_get_int(params, "context", 0);
    if (ctx_lines > 0) str_appendf(&cmd, " -C %d", ctx_lines);

    /* Escape single quotes: ' → '\'' */
    Str esc_pattern = str_from(pattern);
    str_replace_all(&esc_pattern, "'", "'\\''");
    str_appendf(&cmd, " -- '%s'", esc_pattern.data);
    str_free(&esc_pattern);

    if (path) {
        Str esc_path = str_from(path);
        str_replace_all(&esc_path, "'", "'\\''");
        str_appendf(&cmd, " '%s'", esc_path.data);
        str_free(&esc_path);
    } else {
        str_append(&cmd, " .");
    }

    Str output = str_new(4096);
    ProcessOptions opts = {
        .command = cmd.data,
        .timeout_ms = 30000,
        .on_stdout = collect,
        .ctx = &output,
        .abort_flag = signal,
    };
    ProcessResult result;
    process_run(&opts, &result);

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(output.len > 0 ? output.data : "No matches found", NULL);
    *content_count = 1;
    *details = NULL;
    *terminate = false;

    str_free(&cmd);
    str_free(&output);
    return 0;
}

Tool tool_grep_create(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *pat = cJSON_CreateObject();
    cJSON_AddStringToObject(pat, "type", "string");
    cJSON_AddItemToObject(props, "pattern", pat);

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddItemToObject(props, "path", p);

    cJSON *ic = cJSON_CreateObject();
    cJSON_AddStringToObject(ic, "type", "boolean");
    cJSON_AddItemToObject(props, "ignore_case", ic);

    cJSON *ctx = cJSON_CreateObject();
    cJSON_AddStringToObject(ctx, "type", "integer");
    cJSON_AddItemToObject(props, "context", ctx);

    cJSON_AddItemToObject(params, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(params, "required", req);

    return (Tool){
        .name = "grep",
        .label = "Grep",
        .description = "Search file contents for a pattern",
        .parameters = params,
        .execute = grep_execute,
    };
}
