#include "tools.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_LINES 2000
#define BINARY_CHECK_BYTES 8192

static bool is_binary(const char *data, size_t len) {
    size_t check = len < BINARY_CHECK_BYTES ? len : BINARY_CHECK_BYTES;
    for (size_t i = 0; i < check; i++) {
        if (data[i] == '\0') return true;
    }
    return false;
}

static int read_execute(const char *call_id, cJSON *params, void *signal,
                        void (*on_update)(void *ctx, cJSON *partial), void *update_ctx,
                        ContentBlock **content, int *content_count,
                        cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)update_ctx; (void)terminate;

    const char *path = json_get_string(params, "file_path");
    if (!path) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: file_path is required", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    if (!fs_exists(path)) {
        Str msg = str_new(256);
        str_appendf(&msg, "Error: File does not exist: %s", path);
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text(msg.data, NULL);
        *content_count = 1;
        *details = NULL;
        str_free(&msg);
        return 0;
    }

    size_t file_len;
    char *data = fs_read_file(path, &file_len);
    if (!data) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: Could not read file", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    if (is_binary(data, file_len)) {
        free(data);
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: File appears to be binary", NULL);
        *content_count = 1;
        *details = NULL;
        return 0;
    }

    int offset = json_get_int(params, "offset", 0);
    int limit = json_get_int(params, "limit", MAX_LINES);

    Str result = str_new(file_len + 1024);
    int line_num = 1;
    int lines_output = 0;
    const char *p = data;

    while (*p && lines_output < limit) {
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);

        if (line_num > offset) {
            str_appendf(&result, "%d\t", line_num);
            str_append_len(&result, p, line_len);
            str_append_char(&result, '\n');
            lines_output++;
        }

        line_num++;
        p = eol ? eol + 1 : p + line_len;
    }

    free(data);

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(result.data, NULL);
    *content_count = 1;
    *details = NULL;
    *terminate = false;
    str_free(&result);
    return 0;
}

Tool tool_read_create(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *fp = cJSON_CreateObject();
    cJSON_AddStringToObject(fp, "type", "string");
    cJSON_AddStringToObject(fp, "description", "Absolute path to the file to read");
    cJSON_AddItemToObject(props, "file_path", fp);

    cJSON *off = cJSON_CreateObject();
    cJSON_AddStringToObject(off, "type", "integer");
    cJSON_AddStringToObject(off, "description", "Line number to start reading from (0-indexed)");
    cJSON_AddItemToObject(props, "offset", off);

    cJSON *lim = cJSON_CreateObject();
    cJSON_AddStringToObject(lim, "type", "integer");
    cJSON_AddStringToObject(lim, "description", "Number of lines to read");
    cJSON_AddItemToObject(props, "limit", lim);

    cJSON_AddItemToObject(params, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(params, "required", req);

    return (Tool){
        .name = "read",
        .label = "Read",
        .description = "Read a file from the filesystem",
        .parameters = params,
        .execute = read_execute,
    };
}
