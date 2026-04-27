#ifndef RIG_PERMISSIONS_H
#define RIG_PERMISSIONS_H

#include <stdbool.h>

/*
 * Permission trust levels:
 *   PERM_ASK     — always ask user before executing
 *   PERM_ALLOW   — allow this exact tool+pattern without asking
 *
 * Trust rules stored as array of {tool, pattern} pairs.
 * Pattern matching:
 *   tool="bash", pattern=NULL        → trust ALL bash calls
 *   tool="bash", pattern="git *"     → trust bash calls starting with "git "
 *   tool="bash", pattern="make test" → trust this exact bash command
 *   tool="read", pattern=NULL        → trust all read calls
 *   tool="write", pattern="/home/*"  → trust writes under /home/
 *   tool="*",     pattern=NULL       → trust EVERYTHING (yolo mode)
 */

typedef struct {
    char *tool;      /* tool name, or "*" for all */
    char *pattern;   /* argument pattern (glob-style), NULL = any */
} TrustRule;

typedef struct {
    TrustRule *rules;
    int count;
    int capacity;
    bool yolo;       /* trust everything — no prompts */
} PermissionSet;

PermissionSet *permissions_create(void);
void permissions_free(PermissionSet *ps);

/* Add a trust rule */
void permissions_trust(PermissionSet *ps, const char *tool, const char *pattern);

/* Check if a tool call is trusted. Returns true if allowed without asking. */
bool permissions_check(const PermissionSet *ps, const char *tool, const char *arg_summary);

/* Render a human-readable summary of what's being asked */
char *permissions_describe_call(const char *tool, const char *arg_summary);

#endif
