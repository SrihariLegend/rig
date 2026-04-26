/* test_migrations.c — tests for src/harness/migrations.c */
#include "test.h"
#include "harness/migrations.h"
#include "harness/session.h"
#include "util/fs.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TEST_DIR = "/tmp/pi_test_migrations";

static void cleanup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

/* Helper: create a session at a specific version */
static Session *make_session_at_version(int version) {
    Session *s = session_create(TEST_DIR);
    if (!s) return NULL;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    cJSON *arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "hello");
    cJSON_AddItemToArray(arr, block);
    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);

    s->version = version;
    return s;
}

TEST(migration_current_version_no_op) {
    cleanup_test_dir();
    Session *s = make_session_at_version(SESSION_CURRENT_VERSION);
    ASSERT_FALSE(session_needs_migration(s));
    ASSERT_EQ(session_migrate(s), 0);
    ASSERT_EQ(session_get_version(s), SESSION_CURRENT_VERSION);
    session_free(s);
    cleanup_test_dir();
}

TEST(migration_v1_to_v3) {
    cleanup_test_dir();
    Session *s = make_session_at_version(1);

    /* Clear timestamps to simulate v1 */
    for (int i = 0; i < s->entry_count; i++) {
        s->entries[i].timestamp = 0;
    }

    ASSERT_TRUE(session_needs_migration(s));
    ASSERT_EQ(session_migrate(s), 0);
    ASSERT_EQ(session_get_version(s), SESSION_CURRENT_VERSION);

    /* Timestamps should be backfilled */
    for (int i = 0; i < s->entry_count; i++) {
        ASSERT_TRUE(s->entries[i].timestamp > 0);
    }

    session_free(s);
    cleanup_test_dir();
}

TEST(migration_v2_to_v3) {
    cleanup_test_dir();
    Session *s = make_session_at_version(2);

    ASSERT_TRUE(session_needs_migration(s));
    ASSERT_EQ(session_migrate(s), 0);
    ASSERT_EQ(session_get_version(s), SESSION_CURRENT_VERSION);

    session_free(s);
    cleanup_test_dir();
}

TEST(migration_future_version_warning) {
    cleanup_test_dir();
    Session *s = make_session_at_version(99);

    ASSERT_FALSE(session_needs_migration(s));
    /* Returns 1 (warning) for future versions */
    ASSERT_EQ(session_migrate(s), 1);
    /* Version should remain unchanged */
    ASSERT_EQ(session_get_version(s), 99);

    session_free(s);
    cleanup_test_dir();
}

TEST(migration_preserves_entries) {
    cleanup_test_dir();
    Session *s = make_session_at_version(1);

    /* Add more entries */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "assistant");
    cJSON *arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "world");
    cJSON_AddItemToArray(arr, block);
    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);

    int count_before = s->entry_count;
    ASSERT_EQ(session_migrate(s), 0);
    ASSERT_EQ(s->entry_count, count_before);

    /* Verify entry content is intact */
    ASSERT_NOT_NULL(s->entries[0].id);
    ASSERT_NOT_NULL(s->entries[0].data);

    session_free(s);
    cleanup_test_dir();
}

TEST(migration_flush_updates_file) {
    cleanup_test_dir();
    Session *s = make_session_at_version(1);

    /* Clear timestamps to confirm migration modifies them */
    for (int i = 0; i < s->entry_count; i++) {
        s->entries[i].timestamp = 0;
    }

    ASSERT_EQ(session_migrate(s), 0);

    /* File should exist and be updated */
    ASSERT_TRUE(fs_exists(s->file_path));

    /* Reload and verify */
    Session *loaded = session_load(s->file_path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->entry_count, s->entry_count);

    /* Timestamps should be present in reloaded session */
    for (int i = 0; i < loaded->entry_count; i++) {
        ASSERT_TRUE(loaded->entries[i].timestamp > 0);
    }

    session_free(loaded);
    session_free(s);
    cleanup_test_dir();
}

TEST(migration_null_session) {
    ASSERT_FALSE(session_needs_migration(NULL));
    ASSERT_EQ(session_migrate(NULL), -1);
    ASSERT_EQ(session_get_version(NULL), 0);
}

TEST(migration_v1_backfills_from_mtime) {
    cleanup_test_dir();
    Session *s = make_session_at_version(1);
    session_flush(s);

    /* Clear timestamps */
    for (int i = 0; i < s->entry_count; i++) {
        s->entries[i].timestamp = 0;
    }

    s->version = 1;
    ASSERT_EQ(session_migrate(s), 0);

    /* Should have used mtime-based fallback */
    for (int i = 0; i < s->entry_count; i++) {
        ASSERT_TRUE(s->entries[i].timestamp > 0);
    }

    session_free(s);
    cleanup_test_dir();
}

int main(void) {
    TEST_SUITE("Session Migrations");
    RUN_TEST(migration_current_version_no_op);
    RUN_TEST(migration_v1_to_v3);
    RUN_TEST(migration_v2_to_v3);
    RUN_TEST(migration_future_version_warning);
    RUN_TEST(migration_preserves_entries);
    RUN_TEST(migration_flush_updates_file);
    RUN_TEST(migration_null_session);
    RUN_TEST(migration_v1_backfills_from_mtime);
    TEST_REPORT();
}
