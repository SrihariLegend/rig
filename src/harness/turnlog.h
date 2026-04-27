#ifndef RIG_TURNLOG_H
#define RIG_TURNLOG_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *file_path;
    unsigned char *compressed;
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
} Turn;

typedef struct {
    Turn *turns;
    int count;
    int capacity;
    int max_turns;
    int current_turn_id;
} TurnLog;

TurnLog *turnlog_create(int max_turns);
void turnlog_free(TurnLog *tl);

void turnlog_begin_turn(TurnLog *tl, int msg_index, int store_index);
void turnlog_snapshot_file(TurnLog *tl, const char *path);
Turn *turnlog_current(TurnLog *tl);
Turn *turnlog_latest(TurnLog *tl);

int turnlog_restore_turn(Turn *turn);
void turnlog_pop(TurnLog *tl);

#endif
