#ifndef PI_AI_PROVIDER_ANTHROPIC_H
#define PI_AI_PROVIDER_ANTHROPIC_H

#include "ai/registry.h"

void anthropic_register(void);

int anthropic_stream_simple(const Model *model, const Message *messages, int msg_count,
                            const char *system_prompt, const Tool *tools, int tool_count,
                            const SimpleStreamOptions *options, StreamCallback cb, void *userdata);

#endif
