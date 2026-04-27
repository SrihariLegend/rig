#ifndef RIG_HARNESS_MIGRATIONS_H
#define RIG_HARNESS_MIGRATIONS_H

#include "harness/session.h"
#include <stdbool.h>

#define SESSION_CURRENT_VERSION 3

typedef struct {
    int from_version;
    int to_version;
    int (*migrate)(Session *s);
    char *description;
} SessionMigration;

/* Returns true if session version < SESSION_CURRENT_VERSION. */
bool session_needs_migration(Session *s);

/* Apply all needed migrations in order. Returns 0 on success, 1 on warning (future version). */
int session_migrate(Session *s);

/* Get session version. */
int session_get_version(Session *s);

#endif
