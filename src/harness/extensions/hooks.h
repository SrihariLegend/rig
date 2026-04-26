#ifndef PI_HOOKS_H
#define PI_HOOKS_H

#include "cjson/cJSON.h"
#include <stdbool.h>

typedef bool (*HookHandler)(const char *event, cJSON *data, cJSON **result, void *ctx);

typedef struct {
    int priority;
    HookHandler handler;
    void *ctx;
    char *name;
    char *event;
} HookEntry;

typedef struct {
    HookEntry *entries;
    int count;
    int capacity;
} HookChain;

HookChain *hook_chain_create(void);
void hook_chain_free(HookChain *chain);

int hook_chain_add(HookChain *chain, const char *event, int priority,
                   HookHandler handler, void *ctx, const char *name);
int hook_chain_remove(HookChain *chain, const char *name);

bool hook_chain_fire(HookChain *chain, const char *event, cJSON *data, cJSON **result);

int hook_chain_count(HookChain *chain, const char *event);

#endif
