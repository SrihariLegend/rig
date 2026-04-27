#include "tools.h"
#include "util/str.h"
#include "util/process.h"
#include "util/json.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_OUTPUT_BYTES (512 * 1024)
#define MAX_OUTPUT_LINES 10000

static const char *bash_cwd = NULL;

typedef struct {
    Str output;
    int line_count;
} BashCollector;

static void collect_stdout(const char *data, size_t len, void *ctx) {
    BashCollector *c = ctx;
    if (c->output.len < MAX_OUTPUT_BYTES) {
        size_t remaining = MAX_OUTPUT_BYTES - c->output.len;
        size_t to_add = len < remaining ? len : remaining;
        str_append_len(&c->output, data, to_add);
    }
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n') c->line_count++;
    }
}

static char *strip_terminal_escapes(const char *input, size_t len) {
    Str out = str_new(len + 1);
    size_t i = 0;
    while (i < len) {
        if (input[i] == '\x1b') {
            i++;
            if (i < len && input[i] == '[') {
                i++;
                while (i < len && !isalpha((unsigned char)input[i])) i++;
                if (i < len) i++;
            } else if (i < len && input[i] == ']') {
                i++;
                while (i < len && input[i] != '\x07' && input[i] != '\x1b') i++;
                if (i < len && input[i] == '\x07') i++;
                else if (i + 1 < len && input[i] == '\x1b' && input[i+1] == '\\') i += 2;
            } else if (i < len && (input[i] == '(' || input[i] == ')')) {
                i += (i + 1 < len) ? 2 : 1;
            } else if (i < len) {
                i++;
            }
            continue;
        }
        if (input[i] == '\r') { i++; continue; }
        str_append_char(&out, input[i]);
        i++;
    }
    return str_take(&out);
}

static int bash_execute(const char *call_id, cJSON *params, void *signal,
                        void (*on_update)(void *ctx, cJSON *partial), void *update_ctx,
                        ContentBlock **content, int *content_count,
                        cJSON **details, bool *terminate) {
    (void)call_id; (void)on_update; (void)update_ctx; (void)terminate;

    const char *command = json_get_string(params, "command");
    if (!command || !command[0]) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: command is required", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    LOG_INFO("Bash tool: command='%.200s'", command);

    BashCollector collector = { .output = str_new(4096), .line_count = 0 };

    static const char *bash_env[] = { "TERM=dumb", "NO_COLOR=1", "GIT_TERMINAL_PROMPT=0", NULL };

    ProcessOptions opts = {
        .command = command,
        .cwd = bash_cwd,
        .env = bash_env,
        .timeout_ms = 120000,
        .on_stdout = collect_stdout,
        .on_stderr = collect_stdout,
        .ctx = &collector,
        .abort_flag = signal,
    };

    ProcessResult result;
    int rc = process_run(&opts, &result);

    char *clean = strip_terminal_escapes(collector.output.data, collector.output.len);
    size_t clean_len = strlen(clean);

    Str output = str_new(clean_len + 128);
    if (rc != 0) {
        str_append(&output, "Failed to execute command\n");
    }
    if (result.timed_out) {
        str_append(&output, "[Command timed out after 120s]\n");
    }
    str_append(&output, clean);
    free(clean);

    if (collector.output.len >= MAX_OUTPUT_BYTES) {
        str_append(&output, "\n[Output truncated]");
    }

    LOG_INFO("Bash tool: exit_code=%d output_bytes=%zu", result.exit_code, output.len);

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(output.data, NULL);
    *content_count = 1;

    cJSON *det = cJSON_CreateObject();
    cJSON_AddNumberToObject(det, "exit_code", result.exit_code);
    cJSON_AddBoolToObject(det, "timed_out", result.timed_out);
    *details = det;
    *terminate = false;

    str_free(&output);
    str_free(&collector.output);
    return 0;
}

Tool tool_bash_create(const char *cwd) {
    bash_cwd = cwd;

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();
    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "type", "string");
    cJSON_AddStringToObject(cmd, "description", "The bash command to run");
    cJSON_AddItemToObject(props, "command", cmd);
    cJSON_AddItemToObject(params, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("command"));
    cJSON_AddItemToObject(params, "required", req);

    return (Tool){
        .name = "bash",
        .label = "Bash",
        .description = "Execute a bash command and return its output",
        .parameters = params,
        .execute = bash_execute,
        .execution_mode = EXEC_DEFAULT,
    };
}
