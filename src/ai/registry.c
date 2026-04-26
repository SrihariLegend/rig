#include "registry.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PROVIDERS 32

static ApiProvider providers[MAX_PROVIDERS];
static int provider_count = 0;

void ai_registry_init(void) {
    provider_count = 0;
    memset(providers, 0, sizeof(providers));
}

void ai_registry_cleanup(void) {
    provider_count = 0;
}

void ai_register_provider(const ApiProvider *p) {
    if (!p || !p->api || provider_count >= MAX_PROVIDERS) return;

    for (int i = 0; i < provider_count; i++) {
        if (strcmp(providers[i].api, p->api) == 0) {
            providers[i] = *p;
            return;
        }
    }
    providers[provider_count++] = *p;
}

const ApiProvider *ai_get_provider(const char *api) {
    if (!api) return NULL;
    for (int i = 0; i < provider_count; i++) {
        if (strcmp(providers[i].api, api) == 0) {
            return &providers[i];
        }
    }
    return NULL;
}

void ai_unregister_provider(const char *api) {
    if (!api) return;
    for (int i = 0; i < provider_count; i++) {
        if (strcmp(providers[i].api, api) == 0) {
            memmove(&providers[i], &providers[i + 1],
                    (size_t)(provider_count - i - 1) * sizeof(ApiProvider));
            provider_count--;
            return;
        }
    }
}

int ai_stream_simple(const Model *model, const Message *messages, int msg_count,
                     const char *system_prompt, const Tool *tools, int tool_count,
                     const SimpleStreamOptions *options, StreamCallback cb, void *userdata) {
    if (!model || !model->api) return -1;
    const ApiProvider *p = ai_get_provider(model->api);
    if (!p || !p->stream_simple) return -1;
    return p->stream_simple(model, messages, msg_count, system_prompt,
                            tools, tool_count, options, cb, userdata);
}
