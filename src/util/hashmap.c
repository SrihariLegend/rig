#include "hashmap.h"
#include <stdlib.h>
#include <string.h>

#define MIN_CAP 16
#define LOAD_FACTOR 75

static uint32_t fnv1a(const char *key) {
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static int slot_for(uint32_t hash, int capacity) {
    return (int)(hash & (uint32_t)(capacity - 1));
}

Hashmap *hashmap_create(int initial_capacity, void (*free_value)(void *)) {
    if (initial_capacity < MIN_CAP) initial_capacity = MIN_CAP;
    int cap = MIN_CAP;
    while (cap < initial_capacity) cap *= 2;

    Hashmap *m = calloc(1, sizeof(Hashmap));
    if (!m) return NULL;
    m->entries = calloc((size_t)cap, sizeof(HashmapEntry));
    m->capacity = cap;
    m->free_value = free_value;
    return m;
}

void hashmap_destroy(Hashmap *m) {
    if (!m) return;
    for (int i = 0; i < m->capacity; i++) {
        if (m->entries[i].key) {
            free(m->entries[i].key);
            if (m->free_value) m->free_value(m->entries[i].value);
        }
    }
    free(m->entries);
    free(m);
}

static void hashmap_resize(Hashmap *m) {
    int old_cap = m->capacity;
    HashmapEntry *old = m->entries;

    m->capacity *= 2;
    m->entries = calloc((size_t)m->capacity, sizeof(HashmapEntry));
    m->count = 0;

    for (int i = 0; i < old_cap; i++) {
        if (old[i].key) {
            hashmap_set(m, old[i].key, old[i].value);
            free(old[i].key);
        }
    }
    free(old);
}

void hashmap_set(Hashmap *m, const char *key, void *value) {
    if (m->count * 100 >= m->capacity * LOAD_FACTOR) {
        hashmap_resize(m);
    }

    uint32_t hash = fnv1a(key);
    int idx = slot_for(hash, m->capacity);
    HashmapEntry incoming = {
        .key = strdup(key),
        .value = value,
        .hash = hash,
        .probe_distance = 0,
    };

    for (;;) {
        HashmapEntry *e = &m->entries[idx];

        if (!e->key) {
            *e = incoming;
            m->count++;
            return;
        }

        if (e->hash == hash && strcmp(e->key, key) == 0) {
            if (m->free_value) m->free_value(e->value);
            free(incoming.key);
            e->value = value;
            return;
        }

        if (incoming.probe_distance > e->probe_distance) {
            HashmapEntry tmp = *e;
            *e = incoming;
            incoming = tmp;
        }

        incoming.probe_distance++;
        idx = (idx + 1) & (m->capacity - 1);
    }
}

void *hashmap_get(const Hashmap *m, const char *key) {
    uint32_t hash = fnv1a(key);
    int idx = slot_for(hash, m->capacity);

    for (int d = 0; d < m->capacity; d++) {
        const HashmapEntry *e = &m->entries[idx];
        if (!e->key) return NULL;
        if (e->probe_distance < d) return NULL;
        if (e->hash == hash && strcmp(e->key, key) == 0) return e->value;
        idx = (idx + 1) & (m->capacity - 1);
    }
    return NULL;
}

bool hashmap_has(const Hashmap *m, const char *key) {
    uint32_t hash = fnv1a(key);
    int idx = slot_for(hash, m->capacity);

    for (int d = 0; d < m->capacity; d++) {
        const HashmapEntry *e = &m->entries[idx];
        if (!e->key) return false;
        if (e->probe_distance < d) return false;
        if (e->hash == hash && strcmp(e->key, key) == 0) return true;
        idx = (idx + 1) & (m->capacity - 1);
    }
    return false;
}

bool hashmap_remove(Hashmap *m, const char *key) {
    uint32_t hash = fnv1a(key);
    int idx = slot_for(hash, m->capacity);

    for (int d = 0; d < m->capacity; d++) {
        HashmapEntry *e = &m->entries[idx];
        if (!e->key) return false;
        if (e->probe_distance < d) return false;

        if (e->hash == hash && strcmp(e->key, key) == 0) {
            free(e->key);
            if (m->free_value) m->free_value(e->value);
            m->count--;

            int next = (idx + 1) & (m->capacity - 1);
            while (m->entries[next].key && m->entries[next].probe_distance > 0) {
                m->entries[idx] = m->entries[next];
                m->entries[idx].probe_distance--;
                idx = next;
                next = (next + 1) & (m->capacity - 1);
            }
            memset(&m->entries[idx], 0, sizeof(HashmapEntry));
            return true;
        }

        idx = (idx + 1) & (m->capacity - 1);
    }
    return false;
}

void hashmap_iter(const Hashmap *m, bool (*cb)(const char *key, void *value, void *ctx), void *ctx) {
    for (int i = 0; i < m->capacity; i++) {
        if (m->entries[i].key) {
            if (!cb(m->entries[i].key, m->entries[i].value, ctx)) return;
        }
    }
}

int hashmap_count(const Hashmap *m) {
    return m->count;
}
