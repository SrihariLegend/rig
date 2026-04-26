#include "session.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/json.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

static const char *entry_type_strings[] = {
    "message",
    "thinkingLevelChange",
    "modelChange",
    "compaction",
    "branchSummary",
    "custom",
    "customMessage",
    "label",
    "sessionInfo",
};

static SessionEntryType entry_type_from_string(const char *str) {
    for (int i = 0; i < 9; i++) {
        if (strcmp(str, entry_type_strings[i]) == 0) {
            return (SessionEntryType)i;
        }
    }
    return ENTRY_MESSAGE;
}

static const char *entry_type_to_string(SessionEntryType type) {
    if (type >= 0 && type < 9) {
        return entry_type_strings[type];
    }
    return "message";
}

char *session_generate_id(void) {
    static bool seeded = false;
    if (!seeded) {
        int fd = open("/dev/urandom", O_RDONLY);
        unsigned int seed;
        if (fd >= 0 && read(fd, &seed, sizeof(seed)) == sizeof(seed)) {
            srand(seed);
            close(fd);
        } else {
            srand((unsigned int)time(NULL));
        }
        seeded = true;
    }

    char *id = malloc(17);
    if (!id) return NULL;

    for (int i = 0; i < 16; i++) {
        int r = rand() % 16;
        id[i] = "0123456789abcdef"[r];
    }
    id[16] = '\0';

    return id;
}

static void session_entry_free(SessionEntry *e) {
    if (!e) return;
    free(e->id);
    free(e->parent_id);
    if (e->data) cJSON_Delete(e->data);
}

static SessionEntry *session_entry_create(const char *parent_id, SessionEntryType type, cJSON *data) {
    SessionEntry *e = calloc(1, sizeof(SessionEntry));
    if (!e) return NULL;

    e->id = session_generate_id();
    if (!e->id) {
        free(e);
        return NULL;
    }

    e->parent_id = parent_id ? strdup(parent_id) : NULL;
    e->type = type;
    e->data = data ? cJSON_Duplicate(data, true) : cJSON_CreateObject();
    e->timestamp = (int64_t)time(NULL) * 1000;

    return e;
}

static SessionEntry *session_entry_from_json(cJSON *json) {
    if (!json) return NULL;

    SessionEntry *e = calloc(1, sizeof(SessionEntry));
    if (!e) return NULL;

    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *parent_id = cJSON_GetObjectItem(json, "parentId");
    cJSON *type = cJSON_GetObjectItem(json, "type");
    cJSON *data = cJSON_GetObjectItem(json, "data");
    cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

    if (!id || !cJSON_IsString(id)) {
        free(e);
        return NULL;
    }

    e->id = strdup(id->valuestring);
    e->parent_id = (parent_id && cJSON_IsString(parent_id)) ? strdup(parent_id->valuestring) : NULL;
    e->type = (type && cJSON_IsString(type)) ? entry_type_from_string(type->valuestring) : ENTRY_MESSAGE;
    e->data = data ? cJSON_Duplicate(data, true) : cJSON_CreateObject();
    e->timestamp = (timestamp && cJSON_IsNumber(timestamp)) ? (int64_t)timestamp->valuedouble : (int64_t)time(NULL) * 1000;

    return e;
}

static cJSON *session_entry_to_json(const SessionEntry *e) {
    if (!e) return NULL;

    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;

    cJSON_AddStringToObject(json, "id", e->id);
    if (e->parent_id) {
        cJSON_AddStringToObject(json, "parentId", e->parent_id);
    } else {
        cJSON_AddNullToObject(json, "parentId");
    }
    cJSON_AddStringToObject(json, "type", entry_type_to_string(e->type));
    if (e->data) {
        cJSON_AddItemToObject(json, "data", cJSON_Duplicate(e->data, true));
    } else {
        cJSON_AddItemToObject(json, "data", cJSON_CreateObject());
    }
    cJSON_AddNumberToObject(json, "timestamp", (double)e->timestamp);

    return json;
}

static int session_ensure_capacity(Session *s, int additional) {
    if (!s) return -1;

    int required = s->entry_count + additional;
    if (required <= s->entry_capacity) return 0;

    int new_cap = s->entry_capacity == 0 ? 16 : s->entry_capacity * 2;
    while (new_cap < required) new_cap *= 2;

    SessionEntry *new_entries = realloc(s->entries, new_cap * sizeof(SessionEntry));
    if (!new_entries) return -1;

    s->entries = new_entries;
    s->entry_capacity = new_cap;
    return 0;
}

Session *session_create(const char *sessions_dir) {
    if (!sessions_dir) return NULL;

    Session *s = calloc(1, sizeof(Session));
    if (!s) return NULL;

    s->session_id = session_generate_id();
    if (!s->session_id) {
        free(s);
        return NULL;
    }

    char filename[64];
    snprintf(filename, sizeof(filename), "%s.jsonl", s->session_id);
    s->file_path = fs_join(sessions_dir, filename);
    if (!s->file_path) {
        free(s->session_id);
        free(s);
        return NULL;
    }

    s->version = 3;
    s->leaf_id = NULL;
    s->entries = NULL;
    s->entry_count = 0;
    s->entry_capacity = 0;

    fs_mkdir_p(sessions_dir);

    return s;
}

Session *session_load(const char *path) {
    if (!path) return NULL;

    size_t file_len;
    char *content = fs_read_file(path, &file_len);
    if (!content) return NULL;

    Session *s = calloc(1, sizeof(Session));
    if (!s) {
        free(content);
        return NULL;
    }

    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    char *dot = strrchr(filename, '.');
    if (dot) {
        s->session_id = strndup(filename, dot - filename);
    } else {
        s->session_id = strdup(filename);
    }

    s->file_path = strdup(path);
    s->version = 3;
    s->entries = NULL;
    s->entry_count = 0;
    s->entry_capacity = 0;
    s->leaf_id = NULL;

    char *line_start = content;
    char *line_end;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        size_t line_len = line_end - line_start;
        if (line_len > 0) {
            char *line = strndup(line_start, line_len);
            if (line) {
                cJSON *json = cJSON_Parse(line);
                if (json) {
                    SessionEntry *entry = session_entry_from_json(json);
                    if (entry) {
                        if (session_ensure_capacity(s, 1) == 0) {
                            s->entries[s->entry_count++] = *entry;

                            if (entry->type == ENTRY_MESSAGE ||
                                entry->type == ENTRY_CUSTOM_MESSAGE ||
                                entry->type == ENTRY_THINKING_LEVEL_CHANGE ||
                                entry->type == ENTRY_MODEL_CHANGE) {
                                free(s->leaf_id);
                                s->leaf_id = strdup(entry->id);
                            }

                            free(entry);
                        } else {
                            session_entry_free(entry);
                            free(entry);
                        }
                    }
                    cJSON_Delete(json);
                }
                free(line);
            }
        }
        line_start = line_end + 1;
    }

    if (line_start < content + file_len && strlen(line_start) > 0) {
        cJSON *json = cJSON_Parse(line_start);
        if (json) {
            SessionEntry *entry = session_entry_from_json(json);
            if (entry) {
                if (session_ensure_capacity(s, 1) == 0) {
                    s->entries[s->entry_count++] = *entry;

                    if (entry->type == ENTRY_MESSAGE ||
                        entry->type == ENTRY_CUSTOM_MESSAGE ||
                        entry->type == ENTRY_THINKING_LEVEL_CHANGE ||
                        entry->type == ENTRY_MODEL_CHANGE) {
                        free(s->leaf_id);
                        s->leaf_id = strdup(entry->id);
                    }

                    free(entry);
                } else {
                    session_entry_free(entry);
                    free(entry);
                }
            }
            cJSON_Delete(json);
        }
    }

    free(content);
    return s;
}

void session_free(Session *s) {
    if (!s) return;

    free(s->session_id);
    free(s->file_path);
    free(s->leaf_id);

    for (int i = 0; i < s->entry_count; i++) {
        session_entry_free(&s->entries[i]);
    }
    free(s->entries);

    free(s);
}

int session_append(Session *s, SessionEntryType type, cJSON *data) {
    if (!s) return -1;

    SessionEntry *entry = session_entry_create(s->leaf_id, type, data);
    if (!entry) return -1;

    if (session_ensure_capacity(s, 1) != 0) {
        session_entry_free(entry);
        free(entry);
        return -1;
    }

    s->entries[s->entry_count++] = *entry;

    free(s->leaf_id);
    s->leaf_id = strdup(entry->id);

    free(entry);

    cJSON *json = session_entry_to_json(&s->entries[s->entry_count - 1]);
    if (!json) return -1;

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!json_str) return -1;

    Str line = str_from(json_str);
    free(json_str);
    str_append_char(&line, '\n');

    int result = 0;
    if (s->file_path) {
        result = fs_append_file(s->file_path, line.data, line.len);
    }
    str_free(&line);

    return result;
}

int session_flush(Session *s) {
    if (!s) return -1;

    Str content = str_new(1024);

    for (int i = 0; i < s->entry_count; i++) {
        cJSON *json = session_entry_to_json(&s->entries[i]);
        if (json) {
            char *json_str = cJSON_PrintUnformatted(json);
            if (json_str) {
                str_append(&content, json_str);
                str_append_char(&content, '\n');
                free(json_str);
            }
            cJSON_Delete(json);
        }
    }

    int result = fs_write_file(s->file_path, content.data, content.len);
    str_free(&content);

    return result;
}

int session_set_leaf(Session *s, const char *entry_id) {
    if (!s || !entry_id) return -1;

    SessionEntry *entry = session_get_entry(s, entry_id);
    if (!entry) return -1;

    free(s->leaf_id);
    s->leaf_id = strdup(entry_id);

    return 0;
}

SessionEntry *session_get_entry(Session *s, const char *id) {
    if (!s || !id) return NULL;

    for (int i = 0; i < s->entry_count; i++) {
        if (strcmp(s->entries[i].id, id) == 0) {
            return &s->entries[i];
        }
    }

    return NULL;
}

static Message *message_from_json(cJSON *data) {
    if (!data) return NULL;

    cJSON *role_json = cJSON_GetObjectItem(data, "role");
    if (!role_json || !cJSON_IsString(role_json)) return NULL;

    const char *role_str = role_json->valuestring;
    MessageRole role;
    if (strcmp(role_str, "user") == 0) {
        role = ROLE_USER;
    } else if (strcmp(role_str, "assistant") == 0) {
        role = ROLE_ASSISTANT;
    } else if (strcmp(role_str, "tool_result") == 0) {
        role = ROLE_TOOL_RESULT;
    } else {
        return NULL;
    }

    Message *msg = calloc(1, sizeof(Message));
    if (!msg) return NULL;

    msg->role = role;
    msg->timestamp = (int64_t)time(NULL) * 1000;

    cJSON *content_json = cJSON_GetObjectItem(data, "content");
    if (content_json && cJSON_IsArray(content_json)) {
        int content_count = cJSON_GetArraySize(content_json);
        msg->content = calloc(content_count, sizeof(ContentBlock));
        if (!msg->content) {
            free(msg);
            return NULL;
        }
        msg->content_count = content_count;

        for (int i = 0; i < content_count; i++) {
            cJSON *block_json = cJSON_GetArrayItem(content_json, i);
            cJSON *type_json = cJSON_GetObjectItem(block_json, "type");

            if (type_json && cJSON_IsString(type_json)) {
                const char *type_str = type_json->valuestring;

                if (strcmp(type_str, "text") == 0) {
                    cJSON *text_json = cJSON_GetObjectItem(block_json, "text");
                    if (text_json && cJSON_IsString(text_json)) {
                        msg->content[i] = content_text(text_json->valuestring, NULL);
                    }
                } else if (strcmp(type_str, "thinking") == 0) {
                    cJSON *thinking_json = cJSON_GetObjectItem(block_json, "thinking");
                    if (thinking_json && cJSON_IsString(thinking_json)) {
                        msg->content[i] = content_thinking(thinking_json->valuestring, NULL, false);
                    }
                } else if (strcmp(type_str, "image") == 0) {
                    cJSON *data_json = cJSON_GetObjectItem(block_json, "data");
                    cJSON *mime_json = cJSON_GetObjectItem(block_json, "mime_type");
                    if (data_json && cJSON_IsString(data_json)) {
                        const char *mime = (mime_json && cJSON_IsString(mime_json)) ? mime_json->valuestring : "image/jpeg";
                        msg->content[i] = content_image(data_json->valuestring, mime);
                    }
                } else if (strcmp(type_str, "tool_call") == 0) {
                    cJSON *id_json = cJSON_GetObjectItem(block_json, "id");
                    cJSON *name_json = cJSON_GetObjectItem(block_json, "name");
                    cJSON *args_json = cJSON_GetObjectItem(block_json, "arguments");

                    if (id_json && cJSON_IsString(id_json) &&
                        name_json && cJSON_IsString(name_json) && args_json) {
                        msg->content[i] = content_tool_call(
                            id_json->valuestring,
                            name_json->valuestring,
                            args_json
                        );
                    }
                }
            }
        }
    }

    cJSON *api_json = cJSON_GetObjectItem(data, "api");
    if (api_json && cJSON_IsString(api_json)) {
        msg->api = strdup(api_json->valuestring);
    }

    cJSON *provider_json = cJSON_GetObjectItem(data, "provider");
    if (provider_json && cJSON_IsString(provider_json)) {
        msg->provider = strdup(provider_json->valuestring);
    }

    cJSON *model_json = cJSON_GetObjectItem(data, "model_id");
    if (model_json && cJSON_IsString(model_json)) {
        msg->model_id = strdup(model_json->valuestring);
    }

    return msg;
}

int session_build_context(Session *s, Message ***out, int *count) {
    if (!s || !out || !count) return -1;

    *out = NULL;
    *count = 0;

    if (!s->leaf_id) return 0;

    int *path_indices = malloc(s->entry_count * sizeof(int));
    if (!path_indices) return -1;

    int path_len = 0;
    const char *current_id = s->leaf_id;

    while (current_id) {
        SessionEntry *entry = session_get_entry(s, current_id);
        if (!entry) break;

        for (int i = 0; i < s->entry_count; i++) {
            if (strcmp(s->entries[i].id, current_id) == 0) {
                path_indices[path_len++] = i;
                break;
            }
        }

        current_id = entry->parent_id;
    }

    int msg_count = 0;
    for (int i = path_len - 1; i >= 0; i--) {
        if (s->entries[path_indices[i]].type == ENTRY_MESSAGE) {
            msg_count++;
        }
    }

    if (msg_count == 0) {
        free(path_indices);
        return 0;
    }

    Message **messages = calloc(msg_count, sizeof(Message*));
    if (!messages) {
        free(path_indices);
        return -1;
    }

    int msg_idx = 0;
    for (int i = path_len - 1; i >= 0; i--) {
        SessionEntry *entry = &s->entries[path_indices[i]];
        if (entry->type == ENTRY_MESSAGE) {
            Message *msg = message_from_json(entry->data);
            if (msg) {
                messages[msg_idx++] = msg;
            }
        }
    }

    free(path_indices);

    *out = messages;
    *count = msg_idx;

    return 0;
}

int session_branch(Session *s, const char *from_entry_id) {
    if (!s || !from_entry_id) return -1;
    return session_set_leaf(s, from_entry_id);
}

int session_set_name(Session *s, const char *name) {
    if (!s || !name) return -1;

    cJSON *data = cJSON_CreateObject();
    if (!data) return -1;

    cJSON_AddStringToObject(data, "name", name);

    int result = session_append(s, ENTRY_SESSION_INFO, data);
    cJSON_Delete(data);

    return result;
}

const char *session_get_name(Session *s) {
    if (!s) return NULL;

    for (int i = s->entry_count - 1; i >= 0; i--) {
        if (s->entries[i].type == ENTRY_SESSION_INFO) {
            cJSON *name = cJSON_GetObjectItem(s->entries[i].data, "name");
            if (name && cJSON_IsString(name)) {
                return name->valuestring;
            }
        }
    }

    return NULL;
}
