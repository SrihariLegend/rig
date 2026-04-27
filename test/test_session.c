/* test_session.c — tests for src/harness/session.c */
#include "test.h"
#include "harness/session.h"
#include "util/fs.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TEST_DIR = "/tmp/rig_test_sessions";

static void cleanup_test_dir(void) {
    /* crude cleanup */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

TEST(session_create_basic) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->session_id);
    ASSERT_NOT_NULL(s->file_path);
    ASSERT_EQ(s->entry_count, 0);
    ASSERT_EQ(s->version, 3);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_create_null) {
    ASSERT_NULL(session_create(NULL));
}

TEST(session_generate_id) {
    char *id = session_generate_id();
    ASSERT_NOT_NULL(id);
    ASSERT_EQ(strlen(id), 16);
    char *id2 = session_generate_id();
    ASSERT_TRUE(strcmp(id, id2) != 0);
    free(id);
    free(id2);
}

TEST(session_append_basic) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    int rc = session_append(s, ENTRY_MESSAGE, data);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(s->entry_count, 1);
    ASSERT_NOT_NULL(s->leaf_id);
    cJSON_Delete(data);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_append_multiple) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    for (int i = 0; i < 5; i++) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "idx", i);
        session_append(s, ENTRY_MESSAGE, data);
        cJSON_Delete(data);
    }
    ASSERT_EQ(s->entry_count, 5);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_append_chain) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d1 = cJSON_CreateObject();
    session_append(s, ENTRY_MESSAGE, d1);
    cJSON_Delete(d1);

    cJSON *d2 = cJSON_CreateObject();
    session_append(s, ENTRY_MESSAGE, d2);
    cJSON_Delete(d2);

    /* Second entry should have first as parent */
    ASSERT_NOT_NULL(s->entries[1].parent_id);
    ASSERT_STR_EQ(s->entries[1].parent_id, s->entries[0].id);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_load_basic) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);
    char *path = strdup(s->file_path);
    session_free(s);

    Session *loaded = session_load(path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->entry_count, 1);
    ASSERT_NOT_NULL(loaded->leaf_id);
    session_free(loaded);
    free(path);
    cleanup_test_dir();
}

TEST(session_load_null) {
    ASSERT_NULL(session_load(NULL));
}

TEST(session_load_missing) {
    ASSERT_NULL(session_load("/tmp/rig_nonexistent_session.jsonl"));
}

TEST(session_get_entry) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d = cJSON_CreateObject();
    session_append(s, ENTRY_MESSAGE, d);
    cJSON_Delete(d);

    SessionEntry *e = session_get_entry(s, s->entries[0].id);
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->id, s->entries[0].id);

    ASSERT_NULL(session_get_entry(s, "nonexistent"));
    session_free(s);
    cleanup_test_dir();
}

TEST(session_set_leaf) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d = cJSON_CreateObject();
    session_append(s, ENTRY_MESSAGE, d);
    session_append(s, ENTRY_MESSAGE, d);
    cJSON_Delete(d);

    const char *first_id = s->entries[0].id;
    ASSERT_EQ(session_set_leaf(s, first_id), 0);
    ASSERT_STR_EQ(s->leaf_id, first_id);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_branch) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d = cJSON_CreateObject();
    session_append(s, ENTRY_MESSAGE, d);
    session_append(s, ENTRY_MESSAGE, d);
    cJSON_Delete(d);

    ASSERT_EQ(session_branch(s, s->entries[0].id), 0);
    ASSERT_STR_EQ(s->leaf_id, s->entries[0].id);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_set_name_get_name) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    ASSERT_NULL(session_get_name(s));
    session_set_name(s, "my session");
    ASSERT_STR_EQ(session_get_name(s), "my session");
    session_free(s);
    cleanup_test_dir();
}

TEST(session_build_context_empty) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    Message **msgs = NULL;
    int count = 0;
    ASSERT_EQ(session_build_context(s, &msgs, &count), 0);
    ASSERT_EQ(count, 0);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_build_context_with_messages) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d1 = cJSON_CreateObject();
    cJSON_AddStringToObject(d1, "role", "user");
    cJSON *content_arr = cJSON_AddArrayToObject(d1, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "hello");
    cJSON_AddItemToArray(content_arr, block);
    session_append(s, ENTRY_MESSAGE, d1);
    cJSON_Delete(d1);

    Message **msgs = NULL;
    int count = 0;
    ASSERT_EQ(session_build_context(s, &msgs, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(msgs);
    ASSERT_EQ(msgs[0]->role, ROLE_USER);
    for (int i = 0; i < count; i++) message_free(msgs[i]);
    free(msgs);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_flush_and_reload) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d = cJSON_CreateObject();
    session_append(s, ENTRY_MESSAGE, d);
    session_append(s, ENTRY_MESSAGE, d);
    cJSON_Delete(d);
    ASSERT_EQ(session_flush(s), 0);

    Session *loaded = session_load(s->file_path);
    ASSERT_EQ(loaded->entry_count, 2);
    session_free(loaded);
    session_free(s);
    cleanup_test_dir();
}

TEST(session_free_null) {
    session_free(NULL);
}

/* UNICODE: append message with emoji text */
TEST(session_append_emoji) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    cJSON *content_arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "Hello \xF0\x9F\x98\x80 world!");
    cJSON_AddItemToArray(content_arr, block);
    int rc = session_append(s, ENTRY_MESSAGE, data);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(s->entry_count, 1);
    cJSON_Delete(data);
    session_free(s);
    cleanup_test_dir();
}

/* UNICODE: append message with CJK text */
TEST(session_append_cjk) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    cJSON *content_arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95");
    cJSON_AddItemToArray(content_arr, block);
    int rc = session_append(s, ENTRY_MESSAGE, data);
    ASSERT_EQ(rc, 0);
    cJSON_Delete(data);
    session_free(s);
    cleanup_test_dir();
}

/* UNICODE: load session with UTF-8 content */
TEST(session_load_utf8_content) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    cJSON *content_arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80");
    cJSON_AddItemToArray(content_arr, block);
    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);
    session_flush(s);
    char *path = strdup(s->file_path);
    session_free(s);

    Session *loaded = session_load(path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->entry_count, 1);
    session_free(loaded);
    free(path);
    cleanup_test_dir();
}

/* UNICODE: build context with UTF-8 messages */
TEST(session_build_context_utf8) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *d1 = cJSON_CreateObject();
    cJSON_AddStringToObject(d1, "role", "user");
    cJSON *content_arr = cJSON_AddArrayToObject(d1, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "\xED\x95\x9C\xEA\xB8\x80 \xF0\x9F\x98\x80");
    cJSON_AddItemToArray(content_arr, block);
    session_append(s, ENTRY_MESSAGE, d1);
    cJSON_Delete(d1);

    Message **msgs = NULL;
    int count = 0;
    ASSERT_EQ(session_build_context(s, &msgs, &count), 0);
    ASSERT_EQ(count, 1);
    for (int i = 0; i < count; i++) message_free(msgs[i]);
    free(msgs);
    session_free(s);
    cleanup_test_dir();
}

/* RESOURCE EXHAUSTION: session with 5,000 entries */
TEST(session_5000_entries) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    cJSON *content_arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", "msg");
    cJSON_AddItemToArray(content_arr, block);
    for (int i = 0; i < 5000; i++) {
        session_append(s, ENTRY_MESSAGE, data);
    }
    cJSON_Delete(data);
    ASSERT_EQ(s->entry_count, 5000);

    Message **msgs = NULL;
    int count = 0;
    ASSERT_EQ(session_build_context(s, &msgs, &count), 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) message_free(msgs[i]);
    free(msgs);
    session_free(s);
    cleanup_test_dir();
}

/* RESOURCE EXHAUSTION: rapid append/flush cycle */
TEST(session_rapid_append_flush) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    for (int i = 0; i < 100; i++) {
        session_append(s, ENTRY_MESSAGE, data);
        ASSERT_EQ(session_flush(s), 0);
    }
    cJSON_Delete(data);
    ASSERT_EQ(s->entry_count, 100);
    session_free(s);
    cleanup_test_dir();
}

/* ========== ADVERSARIAL: Session Corruption ========== */

TEST(adv_session_load_truncated_jsonl) {
    cleanup_test_dir();
    fs_mkdir_p(TEST_DIR);
    const char *path = "/tmp/rig_test_sessions/truncated.jsonl";
    /* Write a valid first line then a truncated second line */
    const char *data = "{\"id\":\"abc\",\"type\":\"message\",\"data\":{},\"timestamp\":1000}\n{\"id\":\"def\",\"typ";
    fs_write_file(path, data, strlen(data));
    Session *s = session_load(path);
    /* Should not crash. May load partial data or return NULL */
    if (s) {
        ASSERT_TRUE(s->entry_count >= 0);
        session_free(s);
    }
    cleanup_test_dir();
}

TEST(adv_session_load_invalid_json_line) {
    cleanup_test_dir();
    fs_mkdir_p(TEST_DIR);
    const char *path = "/tmp/rig_test_sessions/invalid_line.jsonl";
    const char *data = "{\"id\":\"abc\",\"type\":\"message\",\"data\":{},\"timestamp\":1000}\nnot valid json\n{\"id\":\"ghi\",\"type\":\"message\",\"data\":{},\"timestamp\":1002}\n";
    fs_write_file(path, data, strlen(data));
    Session *s = session_load(path);
    /* Should not crash; may skip the invalid line */
    if (s) {
        ASSERT_TRUE(s->entry_count >= 0);
        session_free(s);
    }
    cleanup_test_dir();
}

TEST(adv_session_load_empty_lines) {
    cleanup_test_dir();
    fs_mkdir_p(TEST_DIR);
    const char *path = "/tmp/rig_test_sessions/empty_lines.jsonl";
    const char *data = "\n\n{\"id\":\"abc\",\"type\":\"message\",\"data\":{},\"timestamp\":1000}\n\n\n";
    fs_write_file(path, data, strlen(data));
    Session *s = session_load(path);
    /* Should not crash */
    if (s) {
        ASSERT_TRUE(s->entry_count >= 0);
        session_free(s);
    }
    cleanup_test_dir();
}

TEST(adv_session_load_binary_garbage) {
    cleanup_test_dir();
    fs_mkdir_p(TEST_DIR);
    const char *path = "/tmp/rig_test_sessions/garbage.jsonl";
    char garbage[512];
    for (int i = 0; i < 512; i++) garbage[i] = (char)(i % 256);
    fs_write_file(path, garbage, 512);
    Session *s = session_load(path);
    /* Should not crash */
    if (s) session_free(s);
    cleanup_test_dir();
}

TEST(adv_session_stress_10000_entries) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    ASSERT_NOT_NULL(s);
    for (int i = 0; i < 10000; i++) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "idx", i);
        int rc = session_append(s, ENTRY_MESSAGE, data);
        cJSON_Delete(data);
        if (rc != 0) break;
    }
    ASSERT_EQ(s->entry_count, 10000);
    /* Flush and reload */
    ASSERT_EQ(session_flush(s), 0);
    Session *loaded = session_load(s->file_path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->entry_count, 10000);
    session_free(loaded);
    session_free(s);
    cleanup_test_dir();
}

int main(void) {
    TEST_SUITE("Session");
    RUN_TEST(session_create_basic);
    RUN_TEST(session_create_null);
    RUN_TEST(session_generate_id);
    RUN_TEST(session_append_basic);
    RUN_TEST(session_append_multiple);
    RUN_TEST(session_append_chain);
    RUN_TEST(session_load_basic);
    RUN_TEST(session_load_null);
    RUN_TEST(session_load_missing);
    RUN_TEST(session_get_entry);
    RUN_TEST(session_set_leaf);
    RUN_TEST(session_branch);
    RUN_TEST(session_set_name_get_name);
    RUN_TEST(session_build_context_empty);
    RUN_TEST(session_build_context_with_messages);
    RUN_TEST(session_flush_and_reload);
    RUN_TEST(session_free_null);

    TEST_SUITE("UNICODE: Sessions");
    RUN_TEST(session_append_emoji);
    RUN_TEST(session_append_cjk);
    RUN_TEST(session_load_utf8_content);
    RUN_TEST(session_build_context_utf8);

    TEST_SUITE("RESOURCE EXHAUSTION: Sessions");
    RUN_TEST(session_5000_entries);
    RUN_TEST(session_rapid_append_flush);

    TEST_SUITE("ADVERSARIAL: Session Corruption");
    RUN_TEST(adv_session_load_truncated_jsonl);
    RUN_TEST(adv_session_load_invalid_json_line);
    RUN_TEST(adv_session_load_empty_lines);
    RUN_TEST(adv_session_load_binary_garbage);
    RUN_TEST(adv_session_stress_10000_entries);

    TEST_REPORT();
}
