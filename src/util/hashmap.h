#ifndef RIG_HASHMAP_H
#define RIG_HASHMAP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *key;
    void *value;
    uint32_t hash;
    int probe_distance;
} HashmapEntry;

typedef struct {
    HashmapEntry *entries;
    int count;
    int capacity;
    void (*free_value)(void *);
} Hashmap;

Hashmap *hashmap_create(int initial_capacity, void (*free_value)(void *));
void hashmap_destroy(Hashmap *m);
void hashmap_set(Hashmap *m, const char *key, void *value);
void *hashmap_get(const Hashmap *m, const char *key);
bool hashmap_has(const Hashmap *m, const char *key);
bool hashmap_remove(Hashmap *m, const char *key);
void hashmap_iter(const Hashmap *m, bool (*cb)(const char *key, void *value, void *ctx), void *ctx);
int hashmap_count(const Hashmap *m);

#endif
