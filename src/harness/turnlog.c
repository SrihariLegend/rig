#include "turnlog.h"
#include "util/fs.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <zlib.h>

/*
 * Binary format per turn file (.rig/snapshots/turn_NNNN.bin):
 *   [4] turn_id
 *   [4] msg_start_index
 *   [4] store_start_index
 *   [4] snapshot_count
 *   For each snapshot:
 *     [4] path_len
 *     [path_len] path (no null terminator)
 *     [1] was_created
 *     [8] original_len
 *     [8] compressed_len
 *     [compressed_len] compressed data
 */

static void write_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void write_u64(FILE *f, uint64_t v) { fwrite(&v, 8, 1, f); }
static uint32_t read_u32(FILE *f) { uint32_t v = 0; fread(&v, 4, 1, f); return v; }
static uint64_t read_u64(FILE *f) { uint64_t v = 0; fread(&v, 8, 1, f); return v; }

/* ---- Helpers ---- */

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

static void turn_path(const TurnLog *tl, int turn_id, char *buf, size_t bufsz) {
    snprintf(buf, bufsz, "%s/turn_%04d.bin", tl->snapshot_dir, turn_id);
}

static size_t turn_compressed_total(const Turn *t) {
    size_t total = 0;
    for (int i = 0; i < t->snapshot_count; i++) {
        size_t next = t->snapshots[i].compressed_len;
        if (total > SIZE_MAX - next) return SIZE_MAX;
        total += next;
    }
    return total;
}

static size_t file_size_safe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long pos = ftell(f);
    fclose(f);
    return pos > 0 ? (size_t)pos : 0;
}

/* ---- Lifecycle ---- */

TurnLog *turnlog_create(const char *project_dir, size_t max_bytes) {
    TurnLog *tl = calloc(1, sizeof(TurnLog));
    if (!tl) return NULL;

    tl->capacity = 32;
    tl->turns = calloc(tl->capacity, sizeof(Turn));
    tl->max_bytes = max_bytes > 0 ? max_bytes : TURNLOG_DEFAULT_MAX_BYTES;
    tl->file_max_bytes = TURNLOG_FILE_MAX_BYTES;
    pthread_mutex_init(&tl->mutex, NULL);

    if (project_dir) {
        size_t len = strlen(project_dir) + 16;
        tl->snapshot_dir = malloc(len);
        if (tl->snapshot_dir) {
            snprintf(tl->snapshot_dir, len, "%s/snapshots", project_dir);
        }
    }

    return tl;
}

void turnlog_free(TurnLog *tl) {
    if (!tl) return;
    for (int i = 0; i < tl->count; i++) {
        turn_free(&tl->turns[i]);
    }
    free(tl->turns);
    free(tl->snapshot_dir);
    pthread_mutex_destroy(&tl->mutex);
    free(tl);
}

/* ---- Eviction (caller must hold mutex) ---- */

static void evict_oldest_locked(TurnLog *tl) {
    if (tl->count == 0) return;

    Turn *oldest = &tl->turns[0];

    if (oldest->on_disk && tl->snapshot_dir) {
        char path[512];
        turn_path(tl, oldest->turn_id, path, sizeof(path));
        if (fs_exists(path)) {
            size_t file_sz = file_size_safe(path);
            unlink(path);
            if (tl->disk_bytes >= file_sz) {
                tl->disk_bytes -= file_sz;
            } else {
                tl->disk_bytes = 0;
            }
            LOG_INFO("turnlog: evicted turn %d (%zu bytes freed)", oldest->turn_id, file_sz);
        }
    }

    turn_free(oldest);
    memmove(tl->turns, tl->turns + 1, (size_t)(tl->count - 1) * sizeof(Turn));
    tl->count--;
}

/* ---- Write turn to disk (caller must hold mutex) ---- */

static int write_turn_locked(const TurnLog *tl, const Turn *t) {
    if (!tl->snapshot_dir || t->snapshot_count == 0) return 0;

    fs_mkdir_p(tl->snapshot_dir);

    char path[512];
    turn_path(tl, t->turn_id, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    write_u32(f, (uint32_t)t->turn_id);
    write_u32(f, (uint32_t)t->msg_start_index);
    write_u32(f, (uint32_t)t->store_start_index);
    write_u32(f, (uint32_t)t->snapshot_count);

    for (int j = 0; j < t->snapshot_count; j++) {
        const FileSnapshot *s = &t->snapshots[j];
        uint32_t path_len = s->file_path ? (uint32_t)strlen(s->file_path) : 0;
        write_u32(f, path_len);
        if (path_len > 0) fwrite(s->file_path, 1, path_len, f);
        uint8_t created = s->was_created ? 1 : 0;
        fwrite(&created, 1, 1, f);
        write_u64(f, (uint64_t)s->original_len);
        write_u64(f, (uint64_t)s->compressed_len);
        if (s->compressed_len > 0 && s->compressed) {
            fwrite(s->compressed, 1, s->compressed_len, f);
        }
    }

    if (ferror(f)) {
        fclose(f);
        unlink(path);
        return -1;
    }

    fclose(f);
    return 0;
}

/* ---- Read turn metadata from disk (no data loaded) ---- */

static int read_turn_from_disk(const char *filepath, Turn *t) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    uint32_t turn_id = read_u32(f);
    uint32_t msg_start = read_u32(f);
    uint32_t store_start = read_u32(f);
    uint32_t snap_count = read_u32(f);

    if (snap_count > 10000) { fclose(f); return -1; }

    memset(t, 0, sizeof(Turn));
    t->turn_id = (int)turn_id;
    t->msg_start_index = (int)msg_start;
    t->store_start_index = (int)store_start;
    t->snapshot_cap = (int)snap_count;
    t->snapshots = calloc(snap_count, sizeof(FileSnapshot));
    if (!t->snapshots) { fclose(f); return -1; }
    t->on_disk = true;

    for (uint32_t j = 0; j < snap_count; j++) {
        FileSnapshot *s = &t->snapshots[j];
        uint32_t path_len = read_u32(f);
        if (path_len > 4096) { fclose(f); turn_free(t); memset(t, 0, sizeof(Turn)); return -1; }

        s->file_path = malloc(path_len + 1);
        if (!s->file_path) { fclose(f); turn_free(t); memset(t, 0, sizeof(Turn)); return -1; }
        if (path_len > 0) {
            if (fread(s->file_path, 1, path_len, f) != path_len) {
                fclose(f); turn_free(t); memset(t, 0, sizeof(Turn)); return -1;
            }
        }
        s->file_path[path_len] = '\0';

        uint8_t created = 0;
        if (fread(&created, 1, 1, f) != 1) {
            fclose(f); turn_free(t); memset(t, 0, sizeof(Turn)); return -1;
        }
        s->was_created = created != 0;

        s->original_len = (size_t)read_u64(f);
        s->compressed_len = (size_t)read_u64(f);

        s->compressed = NULL;
        if (s->compressed_len > 0) {
            if (s->compressed_len > 50 * 1024 * 1024) {
                fclose(f); turn_free(t); memset(t, 0, sizeof(Turn)); return -1;
            }
            fseek(f, (long)s->compressed_len, SEEK_CUR);
        }

        t->snapshot_count++;
    }

    fclose(f);
    return 0;
}

/* Load compressed data for a turn from disk (for restore) */
static int load_turn_data(const TurnLog *tl, Turn *t) {
    if (!tl->snapshot_dir) return -1;

    char path[512];
    turn_path(tl, t->turn_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* Skip header (4 x uint32 = 16 bytes) */
    fseek(f, 16, SEEK_SET);

    for (int j = 0; j < t->snapshot_count; j++) {
        FileSnapshot *s = &t->snapshots[j];
        uint32_t path_len = read_u32(f);
        fseek(f, (long)path_len, SEEK_CUR);
        fseek(f, 1, SEEK_CUR);
        fseek(f, 16, SEEK_CUR);

        free(s->compressed);
        s->compressed = NULL;

        if (s->compressed_len > 0) {
            s->compressed = malloc(s->compressed_len);
            if (!s->compressed) { fclose(f); return -1; }
            if (fread(s->compressed, 1, s->compressed_len, f) != s->compressed_len) {
                free(s->compressed);
                s->compressed = NULL;
                fclose(f);
                return -1;
            }
        }
    }

    fclose(f);
    return 0;
}

/* ---- Public API ---- */

void turnlog_begin_turn(TurnLog *tl, int msg_index, int store_index) {
    if (!tl) return;
    pthread_mutex_lock(&tl->mutex);

    /* Flush previous turn to disk */
    if (tl->count > 0 && tl->snapshot_dir) {
        Turn *prev = &tl->turns[tl->count - 1];
        if (!prev->on_disk && prev->snapshot_count > 0) {
            size_t turn_bytes = turn_compressed_total(prev);

            while (tl->count > 1 && (tl->disk_bytes + turn_bytes > tl->max_bytes)) {
                evict_oldest_locked(tl);
                prev = &tl->turns[tl->count - 1];
            }

            if (tl->count > 0 && write_turn_locked(tl, prev) == 0) {
                tl->disk_bytes += turn_bytes;
                prev->on_disk = true;
                for (int i = 0; i < prev->snapshot_count; i++) {
                    free(prev->snapshots[i].compressed);
                    prev->snapshots[i].compressed = NULL;
                }
            }
        }
    }

    if (tl->count >= tl->capacity) {
        int new_cap = tl->capacity * 2;
        Turn *new_turns = realloc(tl->turns, (size_t)new_cap * sizeof(Turn));
        if (!new_turns) { pthread_mutex_unlock(&tl->mutex); return; }
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
    pthread_mutex_unlock(&tl->mutex);
}

void turnlog_snapshot_file(TurnLog *tl, const char *path) {
    if (!tl || !path) return;
    pthread_mutex_lock(&tl->mutex);

    if (tl->count == 0 || turnlog_budget_exceeded(tl)) {
        pthread_mutex_unlock(&tl->mutex);
        return;
    }

    Turn *t = &tl->turns[tl->count - 1];

    /* Dedup within turn */
    for (int i = 0; i < t->snapshot_count; i++) {
        if (t->snapshots[i].file_path && strcmp(t->snapshots[i].file_path, path) == 0) {
            pthread_mutex_unlock(&tl->mutex);
            return;
        }
    }

    /* Skip large files — release lock during I/O */
    pthread_mutex_unlock(&tl->mutex);

    if (fs_exists(path) && fs_is_file(path)) {
        size_t file_len = 0;
        char *probe = fs_read_file(path, &file_len);
        if (probe) {
            free(probe);
            if (file_len > tl->file_max_bytes) {
                LOG_INFO("turnlog: skip %s (%zu bytes > %zu limit)", path, file_len, tl->file_max_bytes);
                return;
            }
        }
    }

    pthread_mutex_lock(&tl->mutex);

    /* Re-validate after re-acquiring lock */
    if (tl->count == 0) { pthread_mutex_unlock(&tl->mutex); return; }
    t = &tl->turns[tl->count - 1];

    if (t->snapshot_count >= t->snapshot_cap) {
        int new_cap = t->snapshot_cap * 2;
        FileSnapshot *ns = realloc(t->snapshots, (size_t)new_cap * sizeof(FileSnapshot));
        if (!ns) { pthread_mutex_unlock(&tl->mutex); return; }
        t->snapshots = ns;
        t->snapshot_cap = new_cap;
    }

    FileSnapshot *s = &t->snapshots[t->snapshot_count];
    memset(s, 0, sizeof(FileSnapshot));
    s->file_path = strdup(path);
    if (!s->file_path) { pthread_mutex_unlock(&tl->mutex); return; }

    if (!fs_exists(path)) {
        s->was_created = true;
        t->snapshot_count++;
        LOG_INFO("turnlog: snapshot %s (new file)", path);
        pthread_mutex_unlock(&tl->mutex);
        return;
    }

    /* Release lock during file read + compression */
    pthread_mutex_unlock(&tl->mutex);

    size_t file_len = 0;
    char *data = fs_read_file(path, &file_len);
    if (!data) {
        pthread_mutex_lock(&tl->mutex);
        free(s->file_path);
        s->file_path = NULL;
        pthread_mutex_unlock(&tl->mutex);
        return;
    }

    unsigned char *comp_data = NULL;
    size_t comp_len = 0;

    if (file_len > 0) {
        uLongf bound = compressBound((uLong)file_len);
        comp_data = malloc(bound);
        if (!comp_data) {
            free(data);
            pthread_mutex_lock(&tl->mutex);
            free(s->file_path);
            s->file_path = NULL;
            pthread_mutex_unlock(&tl->mutex);
            return;
        }

        uLongf out = bound;
        int rc = compress2(comp_data, &out, (const unsigned char *)data, (uLong)file_len, Z_DEFAULT_COMPRESSION);
        free(data);

        if (rc != Z_OK) {
            free(comp_data);
            pthread_mutex_lock(&tl->mutex);
            free(s->file_path);
            s->file_path = NULL;
            pthread_mutex_unlock(&tl->mutex);
            return;
        }

        unsigned char *shrunk = realloc(comp_data, out);
        comp_data = shrunk ? shrunk : comp_data;
        comp_len = out;
    } else {
        free(data);
    }

    pthread_mutex_lock(&tl->mutex);
    /* Re-validate — turn might have changed while lock was released */
    if (tl->count == 0 || &tl->turns[tl->count - 1] != t) {
        free(comp_data);
        free(s->file_path);
        s->file_path = NULL;
        pthread_mutex_unlock(&tl->mutex);
        return;
    }

    s->original_len = file_len;
    s->compressed = comp_data;
    s->compressed_len = comp_len;
    t->snapshot_count++;

    LOG_INFO("turnlog: snapshot %s (%zu -> %zu bytes)", path, file_len, comp_len);
    pthread_mutex_unlock(&tl->mutex);
}

Turn *turnlog_current(TurnLog *tl) {
    if (!tl || tl->count == 0) return NULL;
    return &tl->turns[tl->count - 1];
}

Turn *turnlog_latest(TurnLog *tl) {
    return turnlog_current(tl);
}

int turnlog_restore_turn(TurnLog *tl, Turn *turn) {
    if (!tl || !turn) return -1;
    pthread_mutex_lock(&tl->mutex);

    /* Load compressed data from disk if needed */
    if (turn->on_disk) {
        if (load_turn_data(tl, turn) != 0) {
            LOG_ERROR("turnlog: failed to load turn %d from disk", turn->turn_id);
            pthread_mutex_unlock(&tl->mutex);
            return -1;
        }
    }

    pthread_mutex_unlock(&tl->mutex);

    int restored = 0;
    for (int i = 0; i < turn->snapshot_count; i++) {
        FileSnapshot *s = &turn->snapshots[i];

        if (s->was_created) {
            if (fs_exists(s->file_path)) {
                unlink(s->file_path);
                LOG_INFO("turnlog: deleted %s (was created this turn)", s->file_path);
                restored++;
            }
            continue;
        }

        if (s->original_len == 0) {
            fs_write_file(s->file_path, "", 0);
            LOG_INFO("turnlog: restored %s (empty)", s->file_path);
            restored++;
            continue;
        }

        if (!s->compressed) continue;

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
    pthread_mutex_lock(&tl->mutex);

    Turn *t = &tl->turns[tl->count - 1];

    if (t->on_disk && tl->snapshot_dir) {
        char path[512];
        turn_path(tl, t->turn_id, path, sizeof(path));
        if (fs_exists(path)) {
            size_t sz = file_size_safe(path);
            unlink(path);
            if (tl->disk_bytes >= sz) tl->disk_bytes -= sz;
            else tl->disk_bytes = 0;
        }
    }

    tl->count--;
    turn_free(&tl->turns[tl->count]);
    memset(&tl->turns[tl->count], 0, sizeof(Turn));

    pthread_mutex_unlock(&tl->mutex);
}

void turnlog_flush(TurnLog *tl) {
    if (!tl || !tl->snapshot_dir) return;
    pthread_mutex_lock(&tl->mutex);

    if (tl->count == 0) { pthread_mutex_unlock(&tl->mutex); return; }

    Turn *t = &tl->turns[tl->count - 1];
    if (t->on_disk || t->snapshot_count == 0) { pthread_mutex_unlock(&tl->mutex); return; }

    size_t turn_bytes = turn_compressed_total(t);

    /* Evict oldest turns until budget allows — never evict current turn */
    while (tl->count > 1 && (tl->disk_bytes + turn_bytes > tl->max_bytes)) {
        evict_oldest_locked(tl);
        t = &tl->turns[tl->count - 1];
    }

    /* If only current turn left and still over budget, write anyway (don't lose current work) */
    if (write_turn_locked(tl, t) == 0) {
        tl->disk_bytes += turn_bytes;
        t->on_disk = true;

        for (int i = 0; i < t->snapshot_count; i++) {
            free(t->snapshots[i].compressed);
            t->snapshots[i].compressed = NULL;
        }

        LOG_INFO("turnlog: flushed turn %d to disk (%zu bytes, total %zu/%zu)",
                 t->turn_id, turn_bytes, tl->disk_bytes, tl->max_bytes);
    }

    pthread_mutex_unlock(&tl->mutex);
}

static int cmp_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int turnlog_scan(TurnLog *tl) {
    if (!tl || !tl->snapshot_dir) return 0;
    if (!fs_exists(tl->snapshot_dir)) return 0;

    DIR *dir = opendir(tl->snapshot_dir);
    if (!dir) return 0;

    /* Collect matching filenames */
    char **files = NULL;
    int file_count = 0;
    int file_cap = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "turn_", 5) != 0) continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".bin") != 0) continue;

        if (file_count >= file_cap) {
            int new_cap = file_cap == 0 ? 32 : file_cap * 2;
            char **nf = realloc(files, (size_t)new_cap * sizeof(char *));
            if (!nf) continue;
            files = nf;
            file_cap = new_cap;
        }

        size_t path_len = strlen(tl->snapshot_dir) + 1 + strlen(ent->d_name) + 1;
        char *full = malloc(path_len);
        if (!full) continue;
        snprintf(full, path_len, "%s/%s", tl->snapshot_dir, ent->d_name);
        files[file_count++] = full;
    }
    closedir(dir);

    /* Sort for deterministic turn order */
    if (file_count > 1) {
        qsort(files, (size_t)file_count, sizeof(char *), cmp_strings);
    }

    pthread_mutex_lock(&tl->mutex);

    int loaded = 0;
    for (int fi = 0; fi < file_count; fi++) {
        size_t file_sz = file_size_safe(files[fi]);

        if (tl->count >= tl->capacity) {
            int new_cap = tl->capacity * 2;
            Turn *nt = realloc(tl->turns, (size_t)new_cap * sizeof(Turn));
            if (!nt) { free(files[fi]); continue; }
            tl->turns = nt;
            tl->capacity = new_cap;
        }

        Turn *t = &tl->turns[tl->count];
        if (read_turn_from_disk(files[fi], t) != 0) { free(files[fi]); continue; }

        if (t->turn_id > tl->current_turn_id) {
            tl->current_turn_id = t->turn_id;
        }

        tl->disk_bytes += file_sz;
        tl->count++;
        loaded++;
        free(files[fi]);
    }
    free(files);

    pthread_mutex_unlock(&tl->mutex);
    LOG_INFO("turnlog: scanned %d turns from %s (%zu bytes)", loaded, tl->snapshot_dir, tl->disk_bytes);
    return loaded;
}

bool turnlog_budget_exceeded(const TurnLog *tl) {
    if (!tl) return false;
    return tl->disk_bytes >= tl->max_bytes;
}
