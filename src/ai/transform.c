#include "transform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ToolCallIdMap *tool_id_map_create(void) {
    ToolCallIdMap *m = calloc(1, sizeof(ToolCallIdMap));
    return m;
}

void tool_id_map_free(ToolCallIdMap *map) {
    if (!map) return;
    for (int i = 0; i < map->count; i++) {
        free(map->mappings[i].original_id);
        free(map->mappings[i].normalized_id);
    }
    free(map->mappings);
    free(map);
}

static bool is_valid_id_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

char *tool_id_normalize(ToolCallIdMap *map, const char *id, int max_len) {
    if (!id) return NULL;

    size_t len = strlen(id);
    bool needs_normalization = (int)len > max_len;

    if (!needs_normalization) {
        for (size_t i = 0; i < len; i++) {
            if (!is_valid_id_char(id[i])) {
                needs_normalization = true;
                break;
            }
        }
    }

    if (!needs_normalization) return strdup(id);

    char normalized[128];
    snprintf(normalized, sizeof(normalized), "tc_%d", map ? map->count : 0);

    if (map) {
        if (map->count >= map->capacity) {
            map->capacity = map->capacity ? map->capacity * 2 : 16;
            map->mappings = realloc(map->mappings, (size_t)map->capacity * sizeof(ToolCallIdMapping));
        }
        map->mappings[map->count].original_id = strdup(id);
        map->mappings[map->count].normalized_id = strdup(normalized);
        map->count++;
    }

    return strdup(normalized);
}

const char *tool_id_lookup_original(const ToolCallIdMap *map, const char *normalized) {
    if (!map || !normalized) return NULL;
    for (int i = 0; i < map->count; i++) {
        if (strcmp(map->mappings[i].normalized_id, normalized) == 0) {
            return map->mappings[i].original_id;
        }
    }
    return normalized;
}

static bool models_match(const Model *a, const Model *b) {
    if (!a || !b) return false;
    return strcmp(a->id, b->id) == 0 && strcmp(a->provider, b->provider) == 0;
}

Message **transform_messages_for_provider(
    const Message *messages, int msg_count,
    const Model *target_model,
    const Model *source_model,
    ToolCallIdMap *id_map,
    int *out_count
) {
    if (!messages || msg_count == 0) {
        *out_count = 0;
        return NULL;
    }

    bool same_model = models_match(target_model, source_model);
    bool target_has_vision = model_supports_images(target_model);

    Message **result = calloc((size_t)msg_count, sizeof(Message *));
    int result_count = 0;

    for (int i = 0; i < msg_count; i++) {
        const Message *m = &messages[i];

        if (m->role == ROLE_ASSISTANT &&
            (m->stop_reason == STOP_ERROR || m->stop_reason == STOP_ABORTED)) {
            continue;
        }

        Message *transformed = message_clone(m);
        if (!transformed) continue;

        for (int j = 0; j < transformed->content_count; j++) {
            ContentBlock *b = &transformed->content[j];

            if (b->type == CONTENT_THINKING && !same_model) {
                if (b->thinking.redacted) {
                    content_block_free(b);
                    memmove(b, b + 1, (size_t)(transformed->content_count - j - 1) * sizeof(ContentBlock));
                    transformed->content_count--;
                    j--;
                    continue;
                }

                char *text = b->thinking.thinking;
                b->thinking.thinking = NULL;
                content_block_free(b);
                b->type = CONTENT_TEXT;
                b->text.text = text;
                b->text.signature = NULL;
            }

            if (b->type == CONTENT_IMAGE && !target_has_vision) {
                content_block_free(b);
                b->type = CONTENT_TEXT;
                b->text.text = strdup("[image omitted - model does not support images]");
                b->text.signature = NULL;
            }

            if (b->type == CONTENT_TOOL_CALL && id_map) {
                char *new_id = tool_id_normalize(id_map, b->tool_call.id, 64);
                free(b->tool_call.id);
                b->tool_call.id = new_id;
            }
        }

        if (transformed->role == ROLE_TOOL_RESULT && id_map && transformed->tool_call_id) {
            char *new_id = tool_id_normalize(id_map, transformed->tool_call_id, 64);
            free(transformed->tool_call_id);
            transformed->tool_call_id = new_id;
        }

        result[result_count++] = transformed;
    }

    *out_count = result_count;
    return result;
}

void transform_messages_free(Message **messages, int count) {
    if (!messages) return;
    for (int i = 0; i < count; i++) {
        message_free(messages[i]);
    }
    free(messages);
}
