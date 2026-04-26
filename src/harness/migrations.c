#include "migrations.h"
#include "util/str.h"
#include "util/fs.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* ---- Migration v1 -> v2: backfill timestamps ---- */

static int migrate_v1_to_v2(Session *s) {
    if (!s) return -1;

    int64_t fallback_ts = 0;
    if (s->file_path) {
        int64_t mtime = fs_mtime(s->file_path);
        if (mtime > 0) {
            fallback_ts = mtime * 1000; /* seconds -> ms */
        }
    }
    if (fallback_ts == 0) {
        fallback_ts = (int64_t)time(NULL) * 1000;
    }

    for (int i = 0; i < s->entry_count; i++) {
        if (s->entries[i].timestamp <= 0) {
            s->entries[i].timestamp = fallback_ts;
        }
    }

    s->version = 2;
    return 0;
}

/* ---- Migration v2 -> v3: add version, normalize type strings ---- */

static const char *normalize_type_string(const char *raw) {
    if (!raw) return "message";

    /* Map common variants to canonical forms */
    if (strcmp(raw, "msg") == 0 || strcmp(raw, "Message") == 0) return "message";
    if (strcmp(raw, "thinking_level_change") == 0) return "thinkingLevelChange";
    if (strcmp(raw, "model_change") == 0) return "modelChange";
    if (strcmp(raw, "branch_summary") == 0) return "branchSummary";
    if (strcmp(raw, "custom_message") == 0) return "customMessage";
    if (strcmp(raw, "session_info") == 0) return "sessionInfo";

    return raw;
}

static SessionEntryType type_from_normalized(const char *str) {
    if (strcmp(str, "message") == 0) return ENTRY_MESSAGE;
    if (strcmp(str, "thinkingLevelChange") == 0) return ENTRY_THINKING_LEVEL_CHANGE;
    if (strcmp(str, "modelChange") == 0) return ENTRY_MODEL_CHANGE;
    if (strcmp(str, "compaction") == 0) return ENTRY_COMPACTION;
    if (strcmp(str, "branchSummary") == 0) return ENTRY_BRANCH_SUMMARY;
    if (strcmp(str, "custom") == 0) return ENTRY_CUSTOM;
    if (strcmp(str, "customMessage") == 0) return ENTRY_CUSTOM_MESSAGE;
    if (strcmp(str, "label") == 0) return ENTRY_LABEL;
    if (strcmp(str, "sessionInfo") == 0) return ENTRY_SESSION_INFO;
    return ENTRY_MESSAGE;
}

static int migrate_v2_to_v3(Session *s) {
    if (!s) return -1;

    /* Normalize entry type strings by re-parsing them through the normalizer.
       The in-memory enum is already canonical, but entries loaded from disk
       with non-standard type strings need their enum value corrected.
       We also patch the data JSON if it contains a "type" field. */

    for (int i = 0; i < s->entry_count; i++) {
        if (s->entries[i].data) {
            cJSON *type_field = cJSON_GetObjectItem(s->entries[i].data, "type");
            if (type_field && cJSON_IsString(type_field)) {
                const char *normalized = normalize_type_string(type_field->valuestring);
                if (strcmp(normalized, type_field->valuestring) != 0) {
                    cJSON_SetValuestring(type_field, normalized);
                    s->entries[i].type = type_from_normalized(normalized);
                }
            }
        }
    }

    s->version = 3;
    return 0;
}

/* ---- Migration table ---- */

static SessionMigration migrations[] = {
    { 1, 2, migrate_v1_to_v2, "Backfill timestamps from file mtime" },
    { 2, 3, migrate_v2_to_v3, "Add version, normalize entry type strings" },
};

static const int migration_count = sizeof(migrations) / sizeof(migrations[0]);

/* ---- Public API ---- */

int session_get_version(Session *s) {
    if (!s) return 0;
    return s->version;
}

bool session_needs_migration(Session *s) {
    if (!s) return false;
    return s->version < SESSION_CURRENT_VERSION;
}

static int session_flush_atomic(Session *s) {
    if (!s || !s->file_path) return -1;

    /* Build the full JSONL content */
    Str content = str_new(1024);

    /* Write a header comment-like entry if version tracking is needed.
       Actually, we just write entries as-is; version is in-memory. */
    for (int i = 0; i < s->entry_count; i++) {
        cJSON *json = cJSON_CreateObject();
        if (!json) {
            str_free(&content);
            return -1;
        }

        cJSON_AddStringToObject(json, "id", s->entries[i].id);
        if (s->entries[i].parent_id) {
            cJSON_AddStringToObject(json, "parentId", s->entries[i].parent_id);
        } else {
            cJSON_AddNullToObject(json, "parentId");
        }

        /* Map type enum back to string */
        static const char *type_strings[] = {
            "message", "thinkingLevelChange", "modelChange", "compaction",
            "branchSummary", "custom", "customMessage", "label", "sessionInfo",
        };
        int t = (int)s->entries[i].type;
        const char *type_str = (t >= 0 && t < 9) ? type_strings[t] : "message";
        cJSON_AddStringToObject(json, "type", type_str);

        if (s->entries[i].data) {
            cJSON_AddItemToObject(json, "data", cJSON_Duplicate(s->entries[i].data, true));
        } else {
            cJSON_AddItemToObject(json, "data", cJSON_CreateObject());
        }

        cJSON_AddNumberToObject(json, "timestamp", (double)s->entries[i].timestamp);

        char *line = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        if (!line) {
            str_free(&content);
            return -1;
        }

        str_append(&content, line);
        str_append_char(&content, '\n');
        free(line);
    }

    /* Atomic write: write to temp file, then rename */
    size_t path_len = strlen(s->file_path);
    char *tmp_path = malloc(path_len + 8);
    if (!tmp_path) {
        str_free(&content);
        return -1;
    }
    snprintf(tmp_path, path_len + 8, "%s.tmp", s->file_path);

    int rc = fs_write_file(tmp_path, content.data, content.len);
    str_free(&content);

    if (rc != 0) {
        free(tmp_path);
        return -1;
    }

    rc = rename(tmp_path, s->file_path);
    free(tmp_path);

    return rc;
}

int session_migrate(Session *s) {
    if (!s) return -1;

    if (s->version > SESSION_CURRENT_VERSION) {
        LOG_WARN("Session version %d is newer than supported version %d; leaving unchanged",
                 s->version, SESSION_CURRENT_VERSION);
        return 1;
    }

    if (s->version == SESSION_CURRENT_VERSION) {
        return 0;
    }

    for (int i = 0; i < migration_count; i++) {
        if (migrations[i].from_version == s->version) {
            LOG_INFO("Migrating session: v%d -> v%d (%s)",
                     migrations[i].from_version, migrations[i].to_version,
                     migrations[i].description);

            int rc = migrations[i].migrate(s);
            if (rc != 0) {
                LOG_ERROR("Migration v%d -> v%d failed",
                          migrations[i].from_version, migrations[i].to_version);
                return -1;
            }

            /* Continue to next migration if needed */
            if (s->version < SESSION_CURRENT_VERSION) {
                i = -1; /* restart loop to find next applicable migration */
            }
        }
    }

    /* Flush to disk atomically */
    if (s->file_path) {
        int rc = session_flush_atomic(s);
        if (rc != 0) {
            LOG_ERROR("Failed to flush migrated session to disk");
            return -1;
        }
    }

    return 0;
}
