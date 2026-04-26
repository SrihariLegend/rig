#include "tools.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/json.h"
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

static int write_execute(const char *call_id, cJSON *params, void *signal,
                         void (*on_update)(void *ctx, cJSON *partial), void *update_ctx,
                         ContentBlock **content, int *content_count,
                         cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)update_ctx; (void)terminate;

    const char *path = json_get_string(params, "file_path");
    const char *file_content = json_get_string(params, "content");

    if (!path || !file_content) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: file_path and content are required", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    char *path_copy = strdup(path);
    char *dir = dirname(path_copy);
    if (!fs_exists(dir)) {
        fs_mkdir_p(dir);
    }
    free(path_copy);

    int rc = fs_write_file(path, file_content, strlen(file_content));
    if (rc != 0) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: Failed to write file", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    Str msg = str_new(256);
    str_appendf(&msg, "Successfully wrote %zu bytes to %s", strlen(file_content), path);
    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(msg.data, NULL);
    *content_count = 1;
    *details = NULL;
    *terminate = false;
    str_free(&msg);
    return 0;
}

Tool tool_write_create(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *fp = cJSON_CreateObject();
    cJSON_AddStringToObject(fp, "type", "string");
    cJSON_AddItemToObject(props, "file_path", fp);

    cJSON *ct = cJSON_CreateObject();
    cJSON_AddStringToObject(ct, "type", "string");
    cJSON_AddItemToObject(props, "content", ct);

    cJSON_AddItemToObject(params, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("file_path"));
    cJSON_AddItemToArray(req, cJSON_CreateString("content"));
    cJSON_AddItemToObject(params, "required", req);

    return (Tool){
        .name = "write",
        .label = "Write",
        .description = "Write content to a file, creating it if it doesn't exist",
        .parameters = params,
        .execute = write_execute,
    };
}
