#include "hooks.h"
#include <stdlib.h>
#include <string.h>

HookChain *hook_chain_create(void) {
    HookChain *chain = calloc(1, sizeof(HookChain));
    if (!chain) return NULL;
    chain->capacity = 16;
    chain->entries = calloc(chain->capacity, sizeof(HookEntry));
    if (!chain->entries) {
        free(chain);
        return NULL;
    }
    return chain;
}

void hook_chain_free(HookChain *chain) {
    if (!chain) return;
    for (int i = 0; i < chain->count; i++) {
        free(chain->entries[i].name);
        free(chain->entries[i].event);
    }
    free(chain->entries);
    free(chain);
}

static int find_insert_pos(HookChain *chain, int priority) {
    for (int i = 0; i < chain->count; i++) {
        if (chain->entries[i].priority > priority) return i;
    }
    return chain->count;
}

int hook_chain_add(HookChain *chain, const char *event, int priority,
                   HookHandler handler, void *ctx, const char *name) {
    if (!chain || !event || !handler) return -1;

    if (chain->count >= chain->capacity) {
        int new_cap = chain->capacity * 2;
        HookEntry *new_entries = realloc(chain->entries, new_cap * sizeof(HookEntry));
        if (!new_entries) return -1;
        chain->entries = new_entries;
        chain->capacity = new_cap;
    }

    int pos = find_insert_pos(chain, priority);

    if (pos < chain->count) {
        memmove(&chain->entries[pos + 1], &chain->entries[pos],
                (chain->count - pos) * sizeof(HookEntry));
    }

    chain->entries[pos] = (HookEntry){
        .priority = priority,
        .handler = handler,
        .ctx = ctx,
        .name = name ? strdup(name) : NULL,
        .event = strdup(event),
    };
    chain->count++;
    return 0;
}

int hook_chain_remove(HookChain *chain, const char *name) {
    if (!chain || !name) return -1;

    for (int i = 0; i < chain->count; i++) {
        if (chain->entries[i].name && strcmp(chain->entries[i].name, name) == 0) {
            free(chain->entries[i].name);
            free(chain->entries[i].event);
            if (i < chain->count - 1) {
                memmove(&chain->entries[i], &chain->entries[i + 1],
                        (chain->count - i - 1) * sizeof(HookEntry));
            }
            chain->count--;
            return 0;
        }
    }
    return -1;
}

bool hook_chain_fire(HookChain *chain, const char *event, cJSON *data, cJSON **result) {
    if (!chain || !event) return true;

    for (int i = 0; i < chain->count; i++) {
        HookEntry *e = &chain->entries[i];
        if (strcmp(e->event, event) != 0 && strcmp(e->event, "*") != 0) continue;

        if (!e->handler(event, data, result, e->ctx)) {
            return false;
        }
    }
    return true;
}

int hook_chain_count(HookChain *chain, const char *event) {
    if (!chain || !event) return 0;
    int count = 0;
    for (int i = 0; i < chain->count; i++) {
        if (strcmp(chain->entries[i].event, event) == 0 ||
            strcmp(chain->entries[i].event, "*") == 0) {
            count++;
        }
    }
    return count;
}
