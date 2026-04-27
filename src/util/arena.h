#ifndef RIG_ARENA_H
#define RIG_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct {
    ArenaBlock *head;
    ArenaBlock *current;
    size_t block_size;
} Arena;

Arena arena_create(size_t block_size);
void *arena_alloc(Arena *a, size_t n);
void *arena_calloc(Arena *a, size_t count, size_t size);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
char *arena_sprintf(Arena *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void arena_reset(Arena *a);
void arena_destroy(Arena *a);
size_t arena_total_allocated(const Arena *a);

#endif
