#include "tools.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/json.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static int ls_execute(const char *call_id, cJSON *params, void *signal,
                      void (*on_update)(void *ctx, cJSON *partial), void *update_ctx,
                      ContentBlock **content, int *content_count,
                      cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)update_ctx; (void)terminate;

    const char *path = json_get_string(params, "path");
    if (!path) path = ".";

    if (!fs_is_dir(path)) {
        Str msg = str_new(256);
        str_appendf(&msg, "Error: Not a directory: %s", path);
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text(msg.data, NULL);
        *content_count = 1;
        *details = NULL;
        str_free(&msg);
        return 0;
    }

    Str output = str_new(4096);

    DIR *d = opendir(path);
    if (!d) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("Error: Could not open directory", NULL);
        *content_count = 1;
        *details = NULL;
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) continue;

        char *full = fs_join(path, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                str_appendf(&output, "%s/\n", ent->d_name);
            } else {
                str_appendf(&output, "%s  (%ld bytes)\n", ent->d_name, (long)st.st_size);
            }
        } else {
            str_appendf(&output, "%s\n", ent->d_name);
        }
        free(full);
    }
    closedir(d);

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(output.len > 0 ? output.data : "(empty directory)", NULL);
    *content_count = 1;
    *details = NULL;
    *terminate = false;
    str_free(&output);
    return 0;
}

Tool tool_ls_create(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON *props = cJSON_CreateObject();

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", "Directory path to list");
    cJSON_AddItemToObject(props, "path", p);

    cJSON_AddItemToObject(params, "properties", props);

    return (Tool){
        .name = "ls",
        .label = "List",
        .description = "List directory contents",
        .parameters = params,
        .execute = ls_execute,
    };
}
