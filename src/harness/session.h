#ifndef PI_HARNESS_SESSION_H
#define PI_HARNESS_SESSION_H

#include "ai/types.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENTRY_MESSAGE,
    ENTRY_THINKING_LEVEL_CHANGE,
    ENTRY_MODEL_CHANGE,
    ENTRY_COMPACTION,
    ENTRY_BRANCH_SUMMARY,
    ENTRY_CUSTOM,
    ENTRY_CUSTOM_MESSAGE,
    ENTRY_LABEL,
    ENTRY_SESSION_INFO,
} SessionEntryType;

typedef struct {
    char *id;
    char *parent_id;
    SessionEntryType type;
    cJSON *data;
    int64_t timestamp;
} SessionEntry;

typedef struct {
    char *session_id;
    char *file_path;
    SessionEntry *entries;
    int entry_count;
    int entry_capacity;
    char *leaf_id;
    int version;
} Session;

// Lifecycle
Session *session_create(const char *sessions_dir);
Session *session_load(const char *path);
void session_free(Session *s);

// Operations
int session_append(Session *s, SessionEntryType type, cJSON *data);
int session_flush(Session *s);

// Navigation
int session_set_leaf(Session *s, const char *entry_id);
SessionEntry *session_get_entry(Session *s, const char *id);

// Context building — walk from root to leaf, return messages
int session_build_context(Session *s, Message ***out, int *count);

// Branching
int session_branch(Session *s, const char *from_entry_id);

// Info
int session_set_name(Session *s, const char *name);
const char *session_get_name(Session *s);

// Utility
char *session_generate_id(void);

#endif
