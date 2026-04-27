#ifndef RIG_VEC_H
#define RIG_VEC_H

#include <stdlib.h>
#include <string.h>

#define Vec(T) struct { T *data; int len; int cap; }

#define vec_init() { .data = NULL, .len = 0, .cap = 0 }

#define vec_push(v, item) do { \
    if ((v)->len >= (v)->cap) { \
        (v)->cap = (v)->cap ? (v)->cap * 2 : 8; \
        (v)->data = realloc((v)->data, (size_t)(v)->cap * sizeof(*(v)->data)); \
    } \
    (v)->data[(v)->len++] = (item); \
} while (0)

#define vec_pop(v) ((v)->data[--(v)->len])

#define vec_last(v) ((v)->data[(v)->len - 1])

#define vec_clear(v) ((v)->len = 0)

#define vec_free(v) do { \
    free((v)->data); \
    (v)->data = NULL; \
    (v)->len = 0; \
    (v)->cap = 0; \
} while (0)

#define vec_remove(v, idx) do { \
    memmove(&(v)->data[(idx)], &(v)->data[(idx) + 1], \
            (size_t)((v)->len - (idx) - 1) * sizeof(*(v)->data)); \
    (v)->len--; \
} while (0)

#define vec_insert(v, idx, item) do { \
    if ((v)->len >= (v)->cap) { \
        (v)->cap = (v)->cap ? (v)->cap * 2 : 8; \
        (v)->data = realloc((v)->data, (size_t)(v)->cap * sizeof(*(v)->data)); \
    } \
    memmove(&(v)->data[(idx) + 1], &(v)->data[(idx)], \
            (size_t)((v)->len - (idx)) * sizeof(*(v)->data)); \
    (v)->data[(idx)] = (item); \
    (v)->len++; \
} while (0)

#define vec_reserve(v, n) do { \
    if ((n) > (v)->cap) { \
        (v)->cap = (n); \
        (v)->data = realloc((v)->data, (size_t)(v)->cap * sizeof(*(v)->data)); \
    } \
} while (0)

#endif
