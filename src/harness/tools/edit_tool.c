#include "tools.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int edit_execute(const char *call_id, cJSON *params, void *signal,
                        void (*on_update)(void *ctx, cJSON *partial), void *update_ctx,
                        ContentBlock **content, int *content_count,
                        cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)update_ctx; (void)terminate;

    const char *path = json_get_string(params, "file_path");
    const char *old_str = json_get_string(params, "old_string");
    const char *new_str = json_get_string(params, "new_string");
    bool replace_all = json_get_bool(params, "replace_all", false);

    if (!path || !old_str || !new_str) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: file_path, old_string, and new_string are required", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
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

    Str file_str = str_from_len(data, file_len);
    free(data);

    if (!replace_all) {
        char *first = strstr(file_str.data, old_str);
        if (!first) {
            str_free(&file_str);
            *content = malloc(sizeof(ContentBlock));
            (*content)[0] = content_text("Error: old_string not found in file", NULL);
            *content_count = 1;
            *details = NULL;
            return -1;
        }

        char *second = strstr(first + strlen(old_str), old_str);
        if (second) {
            str_free(&file_str);
            *content = malloc(sizeof(ContentBlock));
            (*content)[0] = content_text("Error: old_string is not unique in file. Provide more context or use replace_all.", NULL);
            *content_count = 1;
            *details = NULL;
            return -1;
        }

        str_replace(&file_str, old_str, new_str);
    } else {
        int n = str_replace_all(&file_str, old_str, new_str);
        if (n == 0) {
            str_free(&file_str);
            *content = malloc(sizeof(ContentBlock));
            (*content)[0] = content_text("Error: old_string not found in file", NULL);
            *content_count = 1;
            *details = NULL;
            return -1;
        }
    }

    int rc = fs_write_file(path, file_str.data, file_str.len);
    str_free(&file_str);

    if (rc != 0) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: Failed to write file", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text("Successfully edited file", NULL);
    *content_count = 1;
    *details = NULL;
    *terminate = false;
    return 0;
}

Tool tool_edit_create(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *fp = cJSON_CreateObject();
    cJSON_AddStringToObject(fp, "type", "string");
    cJSON_AddItemToObject(props, "file_path", fp);

    cJSON *os = cJSON_CreateObject();
    cJSON_AddStringToObject(os, "type", "string");
    cJSON_AddItemToObject(props, "old_string", os);

    cJSON *ns = cJSON_CreateObject();
    cJSON_AddStringToObject(ns, "type", "string");
    cJSON_AddItemToObject(props, "new_string", ns);

    cJSON *ra = cJSON_CreateObject();
    cJSON_AddStringToObject(ra, "type", "boolean");
    cJSON_AddItemToObject(props, "replace_all", ra);

    cJSON_AddItemToObject(params, "properties", props);
    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("file_path"));
    cJSON_AddItemToArray(req, cJSON_CreateString("old_string"));
    cJSON_AddItemToArray(req, cJSON_CreateString("new_string"));
    cJSON_AddItemToObject(params, "required", req);

    return (Tool){
        .name = "edit",
        .label = "Edit",
        .description = "Edit a file by replacing old_string with new_string",
        .parameters = params,
        .execute = edit_execute,
    };
}
