#ifndef PI_AI_REGISTRY_H
#define PI_AI_REGISTRY_H

#include "types.h"

typedef struct {
    const char *api;
    int (*stream)(const Model *model, const Message *messages, int msg_count,
                  const char *system_prompt, const Tool *tools, int tool_count,
                  const StreamOptions *options, StreamCallback cb, void *userdata);
    int (*stream_simple)(const Model *model, const Message *messages, int msg_count,
                         const char *system_prompt, const Tool *tools, int tool_count,
                         const SimpleStreamOptions *options, StreamCallback cb, void *userdata);
} ApiProvider;

void ai_registry_init(void);
void ai_registry_cleanup(void);
void ai_register_provider(const ApiProvider *provider);
const ApiProvider *ai_get_provider(const char *api);
void ai_unregister_provider(const char *api);

int ai_stream_simple(const Model *model, const Message *messages, int msg_count,
                     const char *system_prompt, const Tool *tools, int tool_count,
                     const SimpleStreamOptions *options, StreamCallback cb, void *userdata);

#endif
