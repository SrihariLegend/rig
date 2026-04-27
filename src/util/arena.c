#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

struct ArenaBlock {
    ArenaBlock *next;
    size_t size;
    size_t used;
    char data[];
};

static ArenaBlock *block_create(size_t size) {
    ArenaBlock *b = malloc(sizeof(ArenaBlock) + size);
    if (!b) return NULL;
    b->next = NULL;
    b->size = size;
    b->used = 0;
    return b;
}

Arena arena_create(size_t block_size) {
    if (block_size == 0) block_size = 64 * 1024;
    ArenaBlock *b = block_create(block_size);
    return (Arena){ .head = b, .current = b, .block_size = block_size };
}

void *arena_alloc(Arena *a, size_t n) {
    if (n == 0) return NULL;
    n = ALIGN_UP(n, 8);

    if (a->current && (a->current->used + n <= a->current->size)) {
        void *ptr = a->current->data + a->current->used;
        a->current->used += n;
        return ptr;
    }

    size_t new_size = a->block_size > n ? a->block_size : n;
    ArenaBlock *b = block_create(new_size);
    if (!b) return NULL;

    if (a->current) {
        a->current->next = b;
    } else {
        a->head = b;
    }
    a->current = b;

    void *ptr = b->data;
    b->used = n;
    return ptr;
}

void *arena_calloc(Arena *a, size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) return NULL;
    size_t total = count * size;
    void *ptr = arena_alloc(a, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = arena_alloc(a, len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (n < len) len = n;
    char *p = arena_alloc(a, len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}

char *arena_sprintf(Arena *a, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);

    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (needed < 0) {
        va_end(ap2);
        return NULL;
    }

    char *p = arena_alloc(a, (size_t)needed + 1);
    if (p) vsnprintf(p, (size_t)needed + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

void arena_reset(Arena *a) {
    if (!a->head) return;

    ArenaBlock *b = a->head->next;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head->next = NULL;
    a->head->used = 0;
    a->current = a->head;
}

void arena_destroy(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
    a->current = NULL;
}

size_t arena_total_allocated(const Arena *a) {
    size_t total = 0;
    for (ArenaBlock *b = a->head; b; b = b->next) {
        total += b->used;
    }
    return total;
}
