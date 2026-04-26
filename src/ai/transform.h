#ifndef PI_AI_TRANSFORM_H
#define PI_AI_TRANSFORM_H

#include "types.h"

typedef struct {
    char *original_id;
    char *normalized_id;
} ToolCallIdMapping;

typedef struct {
    ToolCallIdMapping *mappings;
    int count;
    int capacity;
} ToolCallIdMap;

ToolCallIdMap *tool_id_map_create(void);
void tool_id_map_free(ToolCallIdMap *map);
char *tool_id_normalize(ToolCallIdMap *map, const char *id, int max_len);
const char *tool_id_lookup_original(const ToolCallIdMap *map, const char *normalized);

Message **transform_messages_for_provider(
    const Message *messages, int msg_count,
    const Model *target_model,
    const Model *source_model,
    ToolCallIdMap *id_map,
    int *out_count
);

void transform_messages_free(Message **messages, int count);

#endif
