#ifndef RIG_TURNLOG_H
#define RIG_TURNLOG_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define TURNLOG_DEFAULT_MAX_BYTES (64 * 1024 * 1024)  /* 64 MB */
#define TURNLOG_FILE_MAX_BYTES    (1 * 1024 * 1024)   /* 1 MB per file */

typedef struct {
    char *file_path;
    unsigned char *compressed;   /* NULL when flushed to disk */
    size_t compressed_len;
    size_t original_len;
    bool was_created;
} FileSnapshot;

typedef struct {
    int turn_id;
    FileSnapshot *snapshots;
    int snapshot_count;
    int snapshot_cap;
    int msg_start_index;
    int store_start_index;
    bool on_disk;                /* true if already persisted */
} Turn;

typedef struct {
    Turn *turns;
    int count;
    int capacity;
    int current_turn_id;

    char *snapshot_dir;          /* .rig/snapshots/ */
    size_t max_bytes;            /* budget cap (compressed total on disk) */
    size_t disk_bytes;           /* current bytes on disk */
    size_t file_max_bytes;       /* skip files larger than this */
    bool budget_warned;          /* notify user once */
    pthread_mutex_t mutex;
} TurnLog;

TurnLog *turnlog_create(const char *project_dir, size_t max_bytes);
void turnlog_free(TurnLog *tl);

void turnlog_begin_turn(TurnLog *tl, int msg_index, int store_index);
void turnlog_snapshot_file(TurnLog *tl, const char *path);
Turn *turnlog_current(TurnLog *tl);
Turn *turnlog_latest(TurnLog *tl);

int turnlog_restore_turn(TurnLog *tl, Turn *turn);
void turnlog_pop(TurnLog *tl);

/* Flush current in-memory turn to disk */
void turnlog_flush(TurnLog *tl);

/* Scan snapshot_dir, build metadata index (no data loaded) */
int turnlog_scan(TurnLog *tl);

/* Returns true if budget exceeded */
bool turnlog_budget_exceeded(const TurnLog *tl);

#endif
