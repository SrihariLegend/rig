#include "str.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#define MIN_CAPACITY 64

// Ensure capacity for at least `additional` more bytes
void str_reserve(Str *s, size_t additional) {
    if (!s || !s->data) return;

    size_t required = s->len + additional + 1; // +1 for null terminator
    if (required <= s->cap) return;

    size_t new_cap = s->cap;
    if (new_cap == 0) new_cap = MIN_CAPACITY;

    while (new_cap < required) {
        new_cap *= 2;
    }

    char *new_data = realloc(s->data, new_cap);
    if (!new_data) {
        // Out of memory - this is fatal for a string builder
        abort();
    }

    s->data = new_data;
    s->cap = new_cap;
}

// Create empty string with initial capacity
Str str_new(size_t initial_cap) {
    if (initial_cap < MIN_CAPACITY) {
        initial_cap = MIN_CAPACITY;
    }

    char *data = malloc(initial_cap);
    if (!data) abort();

    data[0] = '\0';

    return (Str){
        .data = data,
        .len = 0,
        .cap = initial_cap
    };
}

// Create from C string
Str str_from(const char *s) {
    if (!s) {
        return str_new(MIN_CAPACITY);
    }
    return str_from_len(s, strlen(s));
}

// Create from buffer with length
Str str_from_len(const char *s, size_t len) {
    if (!s || len == 0) {
        return str_new(MIN_CAPACITY);
    }

    size_t cap = len + 1;
    if (cap < MIN_CAPACITY) cap = MIN_CAPACITY;

    char *data = malloc(cap);
    if (!data) abort();

    memcpy(data, s, len);
    data[len] = '\0';

    return (Str){
        .data = data,
        .len = len,
        .cap = cap
    };
}

// Free string
void str_free(Str *s) {
    if (!s) return;

    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
    s->len = 0;
    s->cap = 0;
}

// Append C string
void str_append(Str *s, const char *data) {
    if (!data) return;
    str_append_len(s, data, strlen(data));
}

// Append with length
void str_append_len(Str *s, const char *data, size_t len) {
    if (!s || !data || len == 0) return;

    str_reserve(s, len);
    memcpy(s->data + s->len, data, len);
    s->len += len;
    s->data[s->len] = '\0';
}

// Append single char
void str_append_char(Str *s, char c) {
    if (!s) return;

    str_reserve(s, 1);
    s->data[s->len] = c;
    s->len++;
    s->data[s->len] = '\0';
}

// Append formatted string (printf-style)
void str_appendf(Str *s, const char *fmt, ...) {
    if (!s || !fmt) return;

    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);

    // Measure required space
    int needed = vsnprintf(NULL, 0, fmt, args1);
    va_end(args1);

    if (needed < 0) {
        va_end(args2);
        return;
    }

    // Reserve space and write
    str_reserve(s, (size_t)needed);
    vsnprintf(s->data + s->len, (size_t)needed + 1, fmt, args2);
    s->len += (size_t)needed;

    va_end(args2);
}

// Insert at position
void str_insert(Str *s, size_t pos, const char *data) {
    if (!s || !data) return;

    size_t data_len = strlen(data);
    if (data_len == 0) return;

    // Clamp position to valid range
    if (pos > s->len) pos = s->len;

    str_reserve(s, data_len);

    // Move existing content to make room
    if (pos < s->len) {
        memmove(s->data + pos + data_len, s->data + pos, s->len - pos);
    }

    // Insert new content
    memcpy(s->data + pos, data, data_len);
    s->len += data_len;
    s->data[s->len] = '\0';
}

// Clear (reset length to 0, keep capacity)
void str_clear(Str *s) {
    if (!s || !s->data) return;

    s->len = 0;
    s->data[0] = '\0';
}

// Trim whitespace from both ends (modifies in-place)
void str_trim(Str *s) {
    if (!s || !s->data || s->len == 0) return;

    // Find first non-whitespace
    size_t start = 0;
    while (start < s->len && isspace((unsigned char)s->data[start])) {
        start++;
    }

    // All whitespace
    if (start == s->len) {
        str_clear(s);
        return;
    }

    // Find last non-whitespace
    size_t end = s->len;
    while (end > start && isspace((unsigned char)s->data[end - 1])) {
        end--;
    }

    size_t new_len = end - start;

    // Move content if needed
    if (start > 0) {
        memmove(s->data, s->data + start, new_len);
    }

    s->len = new_len;
    s->data[s->len] = '\0';
}

// Check if string starts with prefix
bool str_starts_with(const Str *s, const char *prefix) {
    if (!s || !s->data || !prefix) return false;

    size_t prefix_len = strlen(prefix);
    if (prefix_len > s->len) return false;

    return memcmp(s->data, prefix, prefix_len) == 0;
}

// Check if string ends with suffix
bool str_ends_with(const Str *s, const char *suffix) {
    if (!s || !s->data || !suffix) return false;

    size_t suffix_len = strlen(suffix);
    if (suffix_len > s->len) return false;

    return memcmp(s->data + s->len - suffix_len, suffix, suffix_len) == 0;
}

// Find substring, returns index or -1
int str_find(const Str *s, const char *needle) {
    if (!s || !s->data || !needle) return -1;

    char *found = strstr(s->data, needle);
    if (!found) return -1;

    return (int)(found - s->data);
}

// Replace first occurrence, returns true if replaced
bool str_replace(Str *s, const char *old, const char *new_str) {
    if (!s || !s->data || !old || !new_str) return false;

    int pos = str_find(s, old);
    if (pos < 0) return false;

    size_t old_len = strlen(old);
    size_t new_len = strlen(new_str);

    if (new_len > old_len) {
        // Need more space
        str_reserve(s, new_len - old_len);
    }

    if (new_len != old_len) {
        // Move tail
        memmove(s->data + pos + new_len,
                s->data + pos + old_len,
                s->len - pos - old_len + 1); // +1 for null terminator
    }

    // Copy new string
    memcpy(s->data + pos, new_str, new_len);
    s->len = s->len - old_len + new_len;

    return true;
}

// Replace all occurrences, returns count
int str_replace_all(Str *s, const char *old, const char *new_str) {
    if (!s || !s->data || !old || !new_str) return 0;

    size_t old_len = strlen(old);
    size_t new_len = strlen(new_str);

    if (old_len == 0) return 0;

    // Count occurrences first
    int count = 0;
    char *p = s->data;
    while ((p = strstr(p, old)) != NULL) {
        count++;
        p += old_len;
    }

    if (count == 0) return 0;

    // Calculate size difference
    long long size_diff = (long long)new_len - (long long)old_len;
    size_t new_total_len = (size_t)((long long)s->len + size_diff * count);

    if (size_diff > 0) {
        // Growing: need more space
        str_reserve(s, (size_t)(size_diff * count));
    }

    // Replace from end to beginning to avoid overlapping issues
    if (size_diff != 0) {
        // Need to build new string
        Str result = str_new(new_total_len + 1);

        p = s->data;
        char *found;

        while ((found = strstr(p, old)) != NULL) {
            // Copy everything before the match
            size_t prefix_len = (size_t)(found - p);
            str_append_len(&result, p, prefix_len);

            // Copy replacement
            str_append_len(&result, new_str, new_len);

            p = found + old_len;
        }

        // Copy remaining part
        str_append(&result, p);

        // Swap contents
        free(s->data);
        s->data = result.data;
        s->len = result.len;
        s->cap = result.cap;
    } else {
        // Same length: can replace in-place
        p = s->data;
        while ((p = strstr(p, old)) != NULL) {
            memcpy(p, new_str, new_len);
            p += new_len;
        }
    }

    return count;
}

// Take ownership of internal buffer (caller must free). Str is invalidated.
char *str_take(Str *s) {
    if (!s) return NULL;

    char *data = s->data;
    s->data = NULL;
    s->len = 0;
    s->cap = 0;

    return data;
}

// Clone (deep copy)
Str str_clone(const Str *s) {
    if (!s || !s->data) {
        return str_new(MIN_CAPACITY);
    }

    return str_from_len(s->data, s->len);
}
