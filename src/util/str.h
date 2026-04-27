#ifndef RIG_STR_H
#define RIG_STR_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *data;         // null-terminated
    size_t len;         // string length (excluding null)
    size_t cap;         // allocated capacity
} Str;

// Create empty string with initial capacity
Str str_new(size_t initial_cap);

// Create from C string
Str str_from(const char *s);

// Create from buffer with length
Str str_from_len(const char *s, size_t len);

// Free string
void str_free(Str *s);

// Append C string
void str_append(Str *s, const char *data);

// Append with length
void str_append_len(Str *s, const char *data, size_t len);

// Append single char
void str_append_char(Str *s, char c);

// Append formatted string (printf-style)
void str_appendf(Str *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Insert at position
void str_insert(Str *s, size_t pos, const char *data);

// Clear (reset length to 0, keep capacity)
void str_clear(Str *s);

// Trim whitespace from both ends (modifies in-place)
void str_trim(Str *s);

// Check if string starts/ends with prefix/suffix
bool str_starts_with(const Str *s, const char *prefix);
bool str_ends_with(const Str *s, const char *suffix);

// Find substring, returns index or -1
int str_find(const Str *s, const char *needle);

// Replace first occurrence, returns true if replaced
bool str_replace(Str *s, const char *old, const char *new_str);

// Replace all occurrences, returns count
int str_replace_all(Str *s, const char *old, const char *new_str);

// Take ownership of internal buffer (caller must free). Str is invalidated.
char *str_take(Str *s);

// Clone (deep copy)
Str str_clone(const Str *s);

// Ensure capacity for at least `additional` more bytes
void str_reserve(Str *s, size_t additional);

#endif
