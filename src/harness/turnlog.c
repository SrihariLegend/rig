#include "turnlog.h"
#include "util/fs.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

TurnLog *turnlog_create(int max_turns) {
    TurnLog *tl = calloc(1, sizeof(TurnLog));
    if (!tl) return NULL;
    tl->capacity = 32;
    tl->turns = calloc(tl->capacity, sizeof(Turn));
    tl->max_turns = max_turns;
    return tl;
}

static void snapshot_free(FileSnapshot *s) {
    free(s->file_path);
    free(s->compressed);
}

static void turn_free(Turn *t) {
    for (int i = 0; i < t->snapshot_count; i++) {
        snapshot_free(&t->snapshots[i]);
    }
    free(t->snapshots);
}

void turnlog_free(TurnLog *tl) {
    if (!tl) return;
    for (int i = 0; i < tl->count; i++) {
        turn_free(&tl->turns[i]);
    }
    free(tl->turns);
    free(tl);
}

void turnlog_begin_turn(TurnLog *tl, int msg_index, int store_index) {
    /* Evict oldest if at capacity */
    if (tl->max_turns > 0 && tl->count >= tl->max_turns) {
        turn_free(&tl->turns[0]);
        memmove(tl->turns, tl->turns + 1, (size_t)(tl->count - 1) * sizeof(Turn));
        tl->count--;
    }

    if (tl->count >= tl->capacity) {
        int new_cap = tl->capacity * 2;
        Turn *new_turns = realloc(tl->turns, (size_t)new_cap * sizeof(Turn));
        if (!new_turns) return;
        tl->turns = new_turns;
        tl->capacity = new_cap;
    }

    tl->current_turn_id++;
    Turn *t = &tl->turns[tl->count];
    memset(t, 0, sizeof(Turn));
    t->turn_id = tl->current_turn_id;
    t->msg_start_index = msg_index;
    t->store_start_index = store_index;
    t->snapshot_cap = 4;
    t->snapshots = calloc(t->snapshot_cap, sizeof(FileSnapshot));
    tl->count++;

    LOG_INFO("turnlog: begin turn %d (msg=%d, store=%d)", t->turn_id, msg_index, store_index);
}

void turnlog_snapshot_file(TurnLog *tl, const char *path) {
    if (!tl || tl->count == 0 || !path) return;

    Turn *t = &tl->turns[tl->count - 1];

    /* Check if already snapshotted this file in this turn */
    for (int i = 0; i < t->snapshot_count; i++) {
        if (strcmp(t->snapshots[i].file_path, path) == 0) return;
    }

    if (t->snapshot_count >= t->snapshot_cap) {
        int new_cap = t->snapshot_cap * 2;
        FileSnapshot *ns = realloc(t->snapshots, (size_t)new_cap * sizeof(FileSnapshot));
        if (!ns) return;
        t->snapshots = ns;
        t->snapshot_cap = new_cap;
    }

    FileSnapshot *s = &t->snapshots[t->snapshot_count];
    memset(s, 0, sizeof(FileSnapshot));
    s->file_path = strdup(path);

    if (!fs_exists(path)) {
        s->was_created = true;
        s->compressed = NULL;
        s->compressed_len = 0;
        s->original_len = 0;
        t->snapshot_count++;
        LOG_INFO("turnlog: snapshot %s (new file)", path);
        return;
    }

    size_t file_len = 0;
    char *data = fs_read_file(path, &file_len);
    if (!data) {
        free(s->file_path);
        return;
    }

    s->original_len = file_len;

    if (file_len == 0) {
        s->compressed = NULL;
        s->compressed_len = 0;
        free(data);
        t->snapshot_count++;
        LOG_INFO("turnlog: snapshot %s (empty)", path);
        return;
    }

    uLongf comp_len = compressBound((uLong)file_len);
    unsigned char *comp = malloc(comp_len);
    if (!comp) {
        free(data);
        free(s->file_path);
        return;
    }

    int rc = compress2(comp, &comp_len, (const unsigned char *)data, (uLong)file_len, Z_DEFAULT_COMPRESSION);
    free(data);

    if (rc != Z_OK) {
        free(comp);
        free(s->file_path);
        return;
    }

    /* Shrink to actual size */
    s->compressed = realloc(comp, comp_len);
    if (!s->compressed) s->compressed = comp;
    s->compressed_len = comp_len;
    t->snapshot_count++;

    LOG_INFO("turnlog: snapshot %s (%zu -> %zu bytes, %.0f%% compression)",
             path, file_len, (size_t)comp_len,
             file_len > 0 ? (1.0 - (double)comp_len / file_len) * 100 : 0);
}

Turn *turnlog_current(TurnLog *tl) {
    if (!tl || tl->count == 0) return NULL;
    return &tl->turns[tl->count - 1];
}

Turn *turnlog_latest(TurnLog *tl) {
    return turnlog_current(tl);
}

int turnlog_restore_turn(Turn *turn) {
    if (!turn) return -1;

    int restored = 0;
    for (int i = 0; i < turn->snapshot_count; i++) {
        FileSnapshot *s = &turn->snapshots[i];

        if (s->was_created) {
            /* File was created this turn — delete it */
            if (fs_exists(s->file_path)) {
                unlink(s->file_path);
                LOG_INFO("turnlog: deleted %s (was created this turn)", s->file_path);
                restored++;
            }
            continue;
        }

        if (s->original_len == 0) {
            /* File was empty before — truncate it */
            fs_write_file(s->file_path, "", 0);
            LOG_INFO("turnlog: restored %s (empty)", s->file_path);
            restored++;
            continue;
        }

        /* Decompress and write back */
        unsigned char *data = malloc(s->original_len);
        if (!data) continue;

        uLongf out_len = (uLongf)s->original_len;
        int rc = uncompress(data, &out_len, s->compressed, (uLong)s->compressed_len);
        if (rc != Z_OK) {
            LOG_ERROR("turnlog: decompress failed for %s (rc=%d)", s->file_path, rc);
            free(data);
            continue;
        }

        fs_write_file(s->file_path, (const char *)data, out_len);
        free(data);
        LOG_INFO("turnlog: restored %s (%zu bytes)", s->file_path, (size_t)out_len);
        restored++;
    }

    return restored;
}

void turnlog_pop(TurnLog *tl) {
    if (!tl || tl->count == 0) return;
    tl->count--;
    turn_free(&tl->turns[tl->count]);
    memset(&tl->turns[tl->count], 0, sizeof(Turn));
}
