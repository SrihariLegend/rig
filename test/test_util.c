/* test_util.c — tests for src/util/ modules */
#include "test.h"
#include "util/arena.h"
#include "util/str.h"
#include "util/hashmap.h"
#include "util/json.h"
#include "util/http.h"
#include "util/fs.h"
#include "util/process.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========== Arena ========== */

TEST(arena_create_default) {
    Arena a = arena_create(0);
    ASSERT_NOT_NULL(a.head);
    arena_destroy(&a);
}

TEST(arena_create_custom) {
    Arena a = arena_create(1024);
    ASSERT_NOT_NULL(a.head);
    ASSERT_EQ(a.block_size, 1024);
    arena_destroy(&a);
}

TEST(arena_alloc_basic) {
    Arena a = arena_create(4096);
    void *p = arena_alloc(&a, 100);
    ASSERT_NOT_NULL(p);
    arena_destroy(&a);
}

TEST(arena_alloc_zero) {
    Arena a = arena_create(4096);
    void *p = arena_alloc(&a, 0);
    ASSERT_NULL(p);
    arena_destroy(&a);
}

TEST(arena_alloc_large) {
    Arena a = arena_create(64);
    void *p = arena_alloc(&a, 256);
    ASSERT_NOT_NULL(p);
    arena_destroy(&a);
}

TEST(arena_calloc_zeroed) {
    Arena a = arena_create(4096);
    int *p = arena_calloc(&a, 10, sizeof(int));
    ASSERT_NOT_NULL(p);
    for (int i = 0; i < 10; i++) ASSERT_EQ(p[i], 0);
    arena_destroy(&a);
}

TEST(arena_strdup_basic) {
    Arena a = arena_create(4096);
    char *s = arena_strdup(&a, "hello");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");
    arena_destroy(&a);
}

TEST(arena_strdup_null) {
    Arena a = arena_create(4096);
    char *s = arena_strdup(&a, NULL);
    ASSERT_NULL(s);
    arena_destroy(&a);
}

TEST(arena_strndup_basic) {
    Arena a = arena_create(4096);
    char *s = arena_strndup(&a, "hello world", 5);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");
    arena_destroy(&a);
}

TEST(arena_strndup_longer) {
    Arena a = arena_create(4096);
    char *s = arena_strndup(&a, "hi", 100);
    ASSERT_STR_EQ(s, "hi");
    arena_destroy(&a);
}

TEST(arena_sprintf_basic) {
    Arena a = arena_create(4096);
    char *s = arena_sprintf(&a, "num=%d str=%s", 42, "ok");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "num=42 str=ok");
    arena_destroy(&a);
}

TEST(arena_reset) {
    Arena a = arena_create(4096);
    arena_alloc(&a, 100);
    ASSERT_TRUE(arena_total_allocated(&a) > 0);
    arena_reset(&a);
    ASSERT_EQ(arena_total_allocated(&a), 0);
    arena_destroy(&a);
}

TEST(arena_total_allocated) {
    Arena a = arena_create(4096);
    arena_alloc(&a, 16);
    arena_alloc(&a, 32);
    ASSERT_TRUE(arena_total_allocated(&a) >= 48);
    arena_destroy(&a);
}

TEST(arena_multiple_blocks) {
    Arena a = arena_create(64);
    for (int i = 0; i < 50; i++) arena_alloc(&a, 32);
    ASSERT_TRUE(arena_total_allocated(&a) > 0);
    arena_destroy(&a);
}

/* ========== Str ========== */

TEST(str_new_default) {
    Str s = str_new(0);
    ASSERT_NOT_NULL(s.data);
    ASSERT_EQ(s.len, 0);
    ASSERT_TRUE(s.cap >= 64);
    str_free(&s);
}

TEST(str_from_basic) {
    Str s = str_from("hello");
    ASSERT_STR_EQ(s.data, "hello");
    ASSERT_EQ(s.len, 5);
    str_free(&s);
}

TEST(str_from_null) {
    Str s = str_from(NULL);
    ASSERT_NOT_NULL(s.data);
    ASSERT_EQ(s.len, 0);
    str_free(&s);
}

TEST(str_from_len_basic) {
    Str s = str_from_len("hello world", 5);
    ASSERT_STR_EQ(s.data, "hello");
    ASSERT_EQ(s.len, 5);
    str_free(&s);
}

TEST(str_from_len_zero) {
    Str s = str_from_len("hello", 0);
    ASSERT_EQ(s.len, 0);
    str_free(&s);
}

TEST(str_append_basic) {
    Str s = str_from("hello");
    str_append(&s, " world");
    ASSERT_STR_EQ(s.data, "hello world");
    ASSERT_EQ(s.len, 11);
    str_free(&s);
}

TEST(str_append_null) {
    Str s = str_from("hello");
    str_append(&s, NULL);
    ASSERT_STR_EQ(s.data, "hello");
    str_free(&s);
}

TEST(str_append_len_basic) {
    Str s = str_from("ab");
    str_append_len(&s, "cdef", 2);
    ASSERT_STR_EQ(s.data, "abcd");
    str_free(&s);
}

TEST(str_append_char_basic) {
    Str s = str_from("ab");
    str_append_char(&s, 'c');
    ASSERT_STR_EQ(s.data, "abc");
    str_free(&s);
}

TEST(str_appendf_basic) {
    Str s = str_from("val=");
    str_appendf(&s, "%d", 42);
    ASSERT_STR_EQ(s.data, "val=42");
    str_free(&s);
}

TEST(str_insert_begin) {
    Str s = str_from("world");
    str_insert(&s, 0, "hello ");
    ASSERT_STR_EQ(s.data, "hello world");
    str_free(&s);
}

TEST(str_insert_middle) {
    Str s = str_from("helloworld");
    str_insert(&s, 5, " ");
    ASSERT_STR_EQ(s.data, "hello world");
    str_free(&s);
}

TEST(str_insert_end) {
    Str s = str_from("hello");
    str_insert(&s, 5, " world");
    ASSERT_STR_EQ(s.data, "hello world");
    str_free(&s);
}

TEST(str_insert_beyond_end) {
    Str s = str_from("hello");
    str_insert(&s, 999, "!");
    ASSERT_STR_EQ(s.data, "hello!");
    str_free(&s);
}

TEST(str_clear_basic) {
    Str s = str_from("hello");
    str_clear(&s);
    ASSERT_EQ(s.len, 0);
    ASSERT_STR_EQ(s.data, "");
    ASSERT_TRUE(s.cap > 0);
    str_free(&s);
}

TEST(str_trim_both) {
    Str s = str_from("  hello  ");
    str_trim(&s);
    ASSERT_STR_EQ(s.data, "hello");
    str_free(&s);
}

TEST(str_trim_tabs_newlines) {
    Str s = str_from("\t\nhello\n\t");
    str_trim(&s);
    ASSERT_STR_EQ(s.data, "hello");
    str_free(&s);
}

TEST(str_trim_all_whitespace) {
    Str s = str_from("   ");
    str_trim(&s);
    ASSERT_EQ(s.len, 0);
    str_free(&s);
}

TEST(str_trim_empty) {
    Str s = str_from("");
    str_trim(&s);
    ASSERT_EQ(s.len, 0);
    str_free(&s);
}

TEST(str_starts_with_true) {
    Str s = str_from("hello world");
    ASSERT_TRUE(str_starts_with(&s, "hello"));
    str_free(&s);
}

TEST(str_starts_with_false) {
    Str s = str_from("hello");
    ASSERT_FALSE(str_starts_with(&s, "world"));
    str_free(&s);
}

TEST(str_starts_with_longer) {
    Str s = str_from("hi");
    ASSERT_FALSE(str_starts_with(&s, "hello"));
    str_free(&s);
}

TEST(str_ends_with_true) {
    Str s = str_from("hello world");
    ASSERT_TRUE(str_ends_with(&s, "world"));
    str_free(&s);
}

TEST(str_ends_with_false) {
    Str s = str_from("hello");
    ASSERT_FALSE(str_ends_with(&s, "world"));
    str_free(&s);
}

TEST(str_find_found) {
    Str s = str_from("hello world");
    ASSERT_EQ(str_find(&s, "world"), 6);
    str_free(&s);
}

TEST(str_find_not_found) {
    Str s = str_from("hello");
    ASSERT_EQ(str_find(&s, "xyz"), -1);
    str_free(&s);
}

TEST(str_find_beginning) {
    Str s = str_from("hello");
    ASSERT_EQ(str_find(&s, "hel"), 0);
    str_free(&s);
}

TEST(str_replace_basic) {
    Str s = str_from("hello world");
    ASSERT_TRUE(str_replace(&s, "world", "earth"));
    ASSERT_STR_EQ(s.data, "hello earth");
    str_free(&s);
}

TEST(str_replace_not_found) {
    Str s = str_from("hello");
    ASSERT_FALSE(str_replace(&s, "xyz", "abc"));
    str_free(&s);
}

TEST(str_replace_shorter) {
    Str s = str_from("hello world");
    ASSERT_TRUE(str_replace(&s, "world", "hi"));
    ASSERT_STR_EQ(s.data, "hello hi");
    str_free(&s);
}

TEST(str_replace_longer) {
    Str s = str_from("hi");
    ASSERT_TRUE(str_replace(&s, "hi", "hello world"));
    ASSERT_STR_EQ(s.data, "hello world");
    str_free(&s);
}

TEST(str_replace_all_basic) {
    Str s = str_from("aXbXcXd");
    int count = str_replace_all(&s, "X", "Y");
    ASSERT_EQ(count, 3);
    ASSERT_STR_EQ(s.data, "aYbYcYd");
    str_free(&s);
}

TEST(str_replace_all_growing) {
    Str s = str_from("a.b.c");
    int count = str_replace_all(&s, ".", "::");
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(s.data, "a::b::c");
    str_free(&s);
}

TEST(str_replace_all_shrinking) {
    Str s = str_from("a::b::c");
    int count = str_replace_all(&s, "::", ".");
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(s.data, "a.b.c");
    str_free(&s);
}

TEST(str_replace_all_none) {
    Str s = str_from("hello");
    int count = str_replace_all(&s, "xyz", "abc");
    ASSERT_EQ(count, 0);
    str_free(&s);
}

TEST(str_replace_all_empty_pattern) {
    Str s = str_from("hello");
    int count = str_replace_all(&s, "", "x");
    ASSERT_EQ(count, 0);
    str_free(&s);
}

TEST(str_take_basic) {
    Str s = str_from("hello");
    char *taken = str_take(&s);
    ASSERT_STR_EQ(taken, "hello");
    ASSERT_NULL(s.data);
    ASSERT_EQ(s.len, 0);
    free(taken);
}

TEST(str_clone_basic) {
    Str s = str_from("hello");
    Str c = str_clone(&s);
    ASSERT_STR_EQ(c.data, "hello");
    ASSERT_TRUE(c.data != s.data);
    str_free(&s);
    str_free(&c);
}

TEST(str_reserve_grows) {
    Str s = str_new(64);
    size_t old_cap = s.cap;
    str_reserve(&s, 1024);
    ASSERT_TRUE(s.cap > old_cap);
    str_free(&s);
}

TEST(str_free_null_safe) {
    str_free(NULL);
    Str s = {0};
    str_free(&s);
}

/* UNICODE: str_append with multi-byte UTF-8 */
TEST(str_append_utf8_multibyte) {
    Str s = str_from("hello ");
    /* Append 中文 (U+4E2D U+6587) */
    str_append(&s, "\xE4\xB8\xAD\xE6\x96\x87");
    ASSERT_STR_EQ(s.data, "hello \xE4\xB8\xAD\xE6\x96\x87");
    ASSERT_EQ(s.len, 12); /* 6 + 3 + 3 */
    str_free(&s);
}

/* UNICODE: str_find with UTF-8 needle */
TEST(str_find_utf8_needle) {
    Str s = str_from("hello \xE4\xB8\xAD\xE6\x96\x87 world");
    ASSERT_EQ(str_find(&s, "\xE4\xB8\xAD\xE6\x96\x87"), 6);
    str_free(&s);
}

/* UNICODE: str_replace with UTF-8 strings */
TEST(str_replace_utf8) {
    Str s = str_from("hello world");
    ASSERT_TRUE(str_replace(&s, "world", "\xE4\xB8\xAD\xE6\x96\x87"));
    ASSERT_STR_EQ(s.data, "hello \xE4\xB8\xAD\xE6\x96\x87");
    str_free(&s);
}

/* UNICODE: str_trim with UTF-8 whitespace (non-breaking space U+00A0 = C2 A0) */
TEST(str_trim_utf8_whitespace) {
    /* Non-breaking space (U+00A0 = C2 A0) is NOT isspace() in C locale,
       so str_trim won't trim it. Verify it doesn't crash. */
    Str s = str_from("\xC2\xA0hello\xC2\xA0");
    str_trim(&s);
    /* The NBSP bytes remain since isspace doesn't match them in C locale */
    ASSERT_TRUE(s.len > 0);
    ASSERT_TRUE(strstr(s.data, "hello") != NULL);
    str_free(&s);
}

/* UNICODE: str_starts_with with multi-byte prefix */
TEST(str_starts_with_utf8_prefix) {
    Str s = str_from("\xE4\xB8\xAD\xE6\x96\x87 hello");
    ASSERT_TRUE(str_starts_with(&s, "\xE4\xB8\xAD\xE6\x96\x87"));
    ASSERT_FALSE(str_starts_with(&s, "\xE6\x96\x87"));
    str_free(&s);
}

/* ========== Hashmap ========== */

TEST(hashmap_create_destroy) {
    Hashmap *m = hashmap_create(16, NULL);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(hashmap_count(m), 0);
    hashmap_destroy(m);
}

TEST(hashmap_set_get) {
    Hashmap *m = hashmap_create(16, NULL);
    hashmap_set(m, "key", (void *)42);
    ASSERT_EQ((long long)hashmap_get(m, "key"), 42);
    hashmap_destroy(m);
}

TEST(hashmap_get_missing) {
    Hashmap *m = hashmap_create(16, NULL);
    ASSERT_NULL(hashmap_get(m, "nope"));
    hashmap_destroy(m);
}

TEST(hashmap_has) {
    Hashmap *m = hashmap_create(16, NULL);
    hashmap_set(m, "a", (void *)1);
    ASSERT_TRUE(hashmap_has(m, "a"));
    ASSERT_FALSE(hashmap_has(m, "b"));
    hashmap_destroy(m);
}

TEST(hashmap_overwrite) {
    Hashmap *m = hashmap_create(16, NULL);
    hashmap_set(m, "key", (void *)1);
    hashmap_set(m, "key", (void *)2);
    ASSERT_EQ((long long)hashmap_get(m, "key"), 2);
    ASSERT_EQ(hashmap_count(m), 1);
    hashmap_destroy(m);
}

TEST(hashmap_remove) {
    Hashmap *m = hashmap_create(16, NULL);
    hashmap_set(m, "key", (void *)1);
    ASSERT_TRUE(hashmap_remove(m, "key"));
    ASSERT_FALSE(hashmap_has(m, "key"));
    ASSERT_EQ(hashmap_count(m), 0);
    hashmap_destroy(m);
}

TEST(hashmap_remove_missing) {
    Hashmap *m = hashmap_create(16, NULL);
    ASSERT_FALSE(hashmap_remove(m, "nope"));
    hashmap_destroy(m);
}

TEST(hashmap_many_entries) {
    Hashmap *m = hashmap_create(4, NULL);
    char keys[100][8];
    for (int i = 0; i < 100; i++) {
        snprintf(keys[i], sizeof(keys[i]), "k%d", i);
        hashmap_set(m, keys[i], (void *)(long long)(i + 1));
    }
    ASSERT_EQ(hashmap_count(m), 100);
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ((long long)hashmap_get(m, keys[i]), i + 1);
    }
    hashmap_destroy(m);
}

static bool count_iter(const char *key, void *value, void *ctx) {
    (void)key; (void)value;
    (*(int *)ctx)++;
    return true;
}

TEST(hashmap_iter) {
    Hashmap *m = hashmap_create(16, NULL);
    hashmap_set(m, "a", (void *)1);
    hashmap_set(m, "b", (void *)2);
    hashmap_set(m, "c", (void *)3);
    int count = 0;
    hashmap_iter(m, count_iter, &count);
    ASSERT_EQ(count, 3);
    hashmap_destroy(m);
}

TEST(hashmap_free_value) {
    Hashmap *m = hashmap_create(16, free);
    hashmap_set(m, "key", strdup("value"));
    hashmap_destroy(m);
}

TEST(hashmap_count_after_remove) {
    Hashmap *m = hashmap_create(16, NULL);
    hashmap_set(m, "a", (void *)1);
    hashmap_set(m, "b", (void *)2);
    ASSERT_EQ(hashmap_count(m), 2);
    hashmap_remove(m, "a");
    ASSERT_EQ(hashmap_count(m), 1);
    hashmap_destroy(m);
}

/* ========== JSON ========== */

TEST(json_get_simple) {
    cJSON *root = cJSON_Parse("{\"name\":\"test\"}");
    cJSON *item = json_get(root, "name");
    ASSERT_NOT_NULL(item);
    ASSERT_STR_EQ(item->valuestring, "test");
    cJSON_Delete(root);
}

TEST(json_get_nested) {
    cJSON *root = cJSON_Parse("{\"a\":{\"b\":{\"c\":42}}}");
    cJSON *item = json_get(root, "a.b.c");
    ASSERT_NOT_NULL(item);
    ASSERT_EQ(item->valueint, 42);
    cJSON_Delete(root);
}

TEST(json_get_array) {
    cJSON *root = cJSON_Parse("{\"arr\":[10,20,30]}");
    cJSON *item = json_get(root, "arr[1]");
    ASSERT_NOT_NULL(item);
    ASSERT_EQ(item->valueint, 20);
    cJSON_Delete(root);
}

TEST(json_get_missing) {
    cJSON *root = cJSON_Parse("{\"a\":1}");
    ASSERT_NULL(json_get(root, "b"));
    cJSON_Delete(root);
}

TEST(json_get_null_root) {
    ASSERT_NULL(json_get(NULL, "a"));
}

TEST(json_get_string_basic) {
    cJSON *root = cJSON_Parse("{\"s\":\"hello\"}");
    ASSERT_STR_EQ(json_get_string(root, "s"), "hello");
    cJSON_Delete(root);
}

TEST(json_get_string_wrong_type) {
    cJSON *root = cJSON_Parse("{\"n\":42}");
    ASSERT_NULL(json_get_string(root, "n"));
    cJSON_Delete(root);
}

TEST(json_get_int_basic) {
    cJSON *root = cJSON_Parse("{\"n\":42}");
    ASSERT_EQ(json_get_int(root, "n", -1), 42);
    cJSON_Delete(root);
}

TEST(json_get_int_default) {
    cJSON *root = cJSON_Parse("{}");
    ASSERT_EQ(json_get_int(root, "n", 99), 99);
    cJSON_Delete(root);
}

TEST(json_get_double_basic) {
    cJSON *root = cJSON_Parse("{\"d\":3.14}");
    ASSERT_FLOAT_EQ(json_get_double(root, "d", 0), 3.14, 0.001);
    cJSON_Delete(root);
}

TEST(json_get_bool_true) {
    cJSON *root = cJSON_Parse("{\"b\":true}");
    ASSERT_TRUE(json_get_bool(root, "b", false));
    cJSON_Delete(root);
}

TEST(json_get_bool_false) {
    cJSON *root = cJSON_Parse("{\"b\":false}");
    ASSERT_FALSE(json_get_bool(root, "b", true));
    cJSON_Delete(root);
}

TEST(json_get_bool_default) {
    cJSON *root = cJSON_Parse("{}");
    ASSERT_TRUE(json_get_bool(root, "b", true));
    cJSON_Delete(root);
}

TEST(json_set_string_basic) {
    cJSON *root = cJSON_CreateObject();
    ASSERT_EQ(json_set_string(root, "name", "test"), 0);
    ASSERT_STR_EQ(json_get_string(root, "name"), "test");
    cJSON_Delete(root);
}

TEST(json_set_string_nested) {
    cJSON *root = cJSON_CreateObject();
    ASSERT_EQ(json_set_string(root, "a.b.c", "deep"), 0);
    ASSERT_STR_EQ(json_get_string(root, "a.b.c"), "deep");
    cJSON_Delete(root);
}

TEST(json_set_int_basic) {
    cJSON *root = cJSON_CreateObject();
    ASSERT_EQ(json_set_int(root, "n", 42), 0);
    ASSERT_EQ(json_get_int(root, "n", 0), 42);
    cJSON_Delete(root);
}

TEST(json_set_bool_basic) {
    cJSON *root = cJSON_CreateObject();
    ASSERT_EQ(json_set_bool(root, "b", true), 0);
    ASSERT_TRUE(json_get_bool(root, "b", false));
    cJSON_Delete(root);
}

TEST(json_deep_merge_basic) {
    cJSON *base = cJSON_Parse("{\"a\":1,\"b\":2}");
    cJSON *patch = cJSON_Parse("{\"b\":3,\"c\":4}");
    cJSON *merged = json_deep_merge(base, patch);
    ASSERT_NOT_NULL(merged);
    ASSERT_EQ(json_get_int(merged, "a", 0), 1);
    ASSERT_EQ(json_get_int(merged, "b", 0), 3);
    ASSERT_EQ(json_get_int(merged, "c", 0), 4);
    cJSON_Delete(base);
    cJSON_Delete(patch);
    cJSON_Delete(merged);
}

TEST(json_deep_merge_nested) {
    cJSON *base = cJSON_Parse("{\"x\":{\"a\":1,\"b\":2}}");
    cJSON *patch = cJSON_Parse("{\"x\":{\"b\":3,\"c\":4}}");
    cJSON *merged = json_deep_merge(base, patch);
    ASSERT_EQ(json_get_int(merged, "x.a", 0), 1);
    ASSERT_EQ(json_get_int(merged, "x.b", 0), 3);
    ASSERT_EQ(json_get_int(merged, "x.c", 0), 4);
    cJSON_Delete(base);
    cJSON_Delete(patch);
    cJSON_Delete(merged);
}

TEST(json_deep_merge_null_base) {
    cJSON *patch = cJSON_Parse("{\"a\":1}");
    cJSON *merged = json_deep_merge(NULL, patch);
    ASSERT_EQ(json_get_int(merged, "a", 0), 1);
    cJSON_Delete(patch);
    cJSON_Delete(merged);
}

TEST(json_deep_merge_null_patch) {
    cJSON *base = cJSON_Parse("{\"a\":1}");
    cJSON *merged = json_deep_merge(base, NULL);
    ASSERT_EQ(json_get_int(merged, "a", 0), 1);
    cJSON_Delete(base);
    cJSON_Delete(merged);
}

TEST(json_deep_merge_both_null) {
    ASSERT_NULL(json_deep_merge(NULL, NULL));
}

TEST(json_read_write_file) {
    const char *path = "/tmp/pi_test_json.json";
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "key", "value");
    ASSERT_EQ(json_write_file(path, data), 0);
    cJSON_Delete(data);

    cJSON *loaded = json_read_file(path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(json_get_string(loaded, "key"), "value");
    cJSON_Delete(loaded);
    unlink(path);
}

TEST(json_read_file_missing) {
    ASSERT_NULL(json_read_file("/tmp/nonexistent_pi_test.json"));
}

TEST(json_string_array_basic) {
    const char *strs[] = {"a", "b", "c", NULL};
    cJSON *arr = json_string_array(strs);
    ASSERT_NOT_NULL(arr);
    ASSERT_EQ(cJSON_GetArraySize(arr), 3);
    ASSERT_STR_EQ(cJSON_GetArrayItem(arr, 0)->valuestring, "a");
    ASSERT_STR_EQ(cJSON_GetArrayItem(arr, 2)->valuestring, "c");
    cJSON_Delete(arr);
}

TEST(json_string_array_null) {
    ASSERT_NULL(json_string_array(NULL));
}

/* ========== HTTP (SSE Parser) ========== */

typedef struct { int count; char last_type[64]; char last_data[256]; } SSETestCtx;

static void sse_test_cb(const char *event_type, const char *data, void *ctx) {
    SSETestCtx *t = ctx;
    t->count++;
    strncpy(t->last_type, event_type, 63);
    strncpy(t->last_data, data, 255);
}

TEST(sse_parser_basic) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    ASSERT_NOT_NULL(p);
    const char *chunk = "data: hello\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.last_type, "message");
    ASSERT_STR_EQ(ctx.last_data, "hello");
    sse_parser_destroy(p);
}

TEST(sse_parser_typed_event) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    const char *chunk = "event: done\ndata: finished\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.last_type, "done");
    ASSERT_STR_EQ(ctx.last_data, "finished");
    sse_parser_destroy(p);
}

TEST(sse_parser_multi_data_lines) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    const char *chunk = "data: line1\ndata: line2\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.last_data, "line1\nline2");
    sse_parser_destroy(p);
}

TEST(sse_parser_comment_ignored) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    const char *chunk = ": comment\ndata: real\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.last_data, "real");
    sse_parser_destroy(p);
}

TEST(sse_parser_multiple_events) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    const char *chunk = "data: one\n\ndata: two\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 2);
    ASSERT_STR_EQ(ctx.last_data, "two");
    sse_parser_destroy(p);
}

TEST(sse_parser_chunked_input) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    sse_parser_feed(p, "dat", 3);
    sse_parser_feed(p, "a: hello", 8);
    sse_parser_feed(p, "\n\n", 2);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.last_data, "hello");
    sse_parser_destroy(p);
}

TEST(sse_parser_destroy_null) {
    sse_parser_destroy(NULL);
}

TEST(http_response_free_null) {
    http_response_free(NULL);
    HttpResponse r = {0};
    http_response_free(&r);
}

/* ========== FS ========== */

TEST(fs_write_read) {
    const char *path = "/tmp/pi_test_fs.txt";
    ASSERT_EQ(fs_write_file(path, "hello", 5), 0);
    size_t len = 0;
    char *data = fs_read_file(path, &len);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(len, 5);
    ASSERT_TRUE(memcmp(data, "hello", 5) == 0);
    free(data);
    unlink(path);
}

TEST(fs_append_file) {
    const char *path = "/tmp/pi_test_fs_append.txt";
    unlink(path);
    ASSERT_EQ(fs_write_file(path, "hello", 5), 0);
    ASSERT_EQ(fs_append_file(path, " world", 6), 0);
    size_t len = 0;
    char *data = fs_read_file(path, &len);
    ASSERT_EQ(len, 11);
    ASSERT_TRUE(memcmp(data, "hello world", 11) == 0);
    free(data);
    unlink(path);
}

TEST(fs_exists_true) {
    ASSERT_TRUE(fs_exists("/tmp"));
}

TEST(fs_exists_false) {
    ASSERT_FALSE(fs_exists("/tmp/pi_nonexistent_file_abc"));
}

TEST(fs_is_dir_true) {
    ASSERT_TRUE(fs_is_dir("/tmp"));
}

TEST(fs_is_dir_false) {
    const char *path = "/tmp/pi_test_isdir.txt";
    fs_write_file(path, "x", 1);
    ASSERT_FALSE(fs_is_dir(path));
    unlink(path);
}

TEST(fs_is_file_true) {
    const char *path = "/tmp/pi_test_isfile.txt";
    fs_write_file(path, "x", 1);
    ASSERT_TRUE(fs_is_file(path));
    unlink(path);
}

TEST(fs_is_file_false) {
    ASSERT_FALSE(fs_is_file("/tmp"));
}

TEST(fs_mkdir_p_basic) {
    const char *path = "/tmp/pi_test_mkdir/a/b/c";
    ASSERT_EQ(fs_mkdir_p(path), 0);
    ASSERT_TRUE(fs_is_dir(path));
    rmdir("/tmp/pi_test_mkdir/a/b/c");
    rmdir("/tmp/pi_test_mkdir/a/b");
    rmdir("/tmp/pi_test_mkdir/a");
    rmdir("/tmp/pi_test_mkdir");
}

TEST(fs_mtime_exists) {
    const char *path = "/tmp/pi_test_mtime.txt";
    fs_write_file(path, "x", 1);
    ASSERT_TRUE(fs_mtime(path) > 0);
    unlink(path);
}

TEST(fs_mtime_missing) {
    ASSERT_EQ(fs_mtime("/tmp/pi_nonexistent_xyz"), -1);
}

TEST(fs_join_basic) {
    char *p = fs_join("/home", "user");
    ASSERT_STR_EQ(p, "/home/user");
    free(p);
}

TEST(fs_join_trailing_slash) {
    char *p = fs_join("/home/", "user");
    ASSERT_STR_EQ(p, "/home/user");
    free(p);
}

TEST(fs_join_leading_slash) {
    char *p = fs_join("/home", "/user");
    ASSERT_STR_EQ(p, "/home/user");
    free(p);
}

TEST(fs_join_null) {
    ASSERT_NULL(fs_join(NULL, "user"));
    ASSERT_NULL(fs_join("/home", NULL));
}

TEST(fs_homedir_not_null) {
    ASSERT_NOT_NULL(fs_homedir());
}

TEST(fs_expand_home_tilde) {
    char *p = fs_expand_home("~/test");
    ASSERT_NOT_NULL(p);
    ASSERT_TRUE(p[0] == '/');
    ASSERT_TRUE(strstr(p, "test") != NULL);
    free(p);
}

TEST(fs_expand_home_no_tilde) {
    char *p = fs_expand_home("/absolute/path");
    ASSERT_STR_EQ(p, "/absolute/path");
    free(p);
}

TEST(fs_expand_home_null) {
    char *p = fs_expand_home(NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p, "");
    free(p);
}

TEST(fs_read_file_missing) {
    ASSERT_NULL(fs_read_file("/tmp/pi_nonexistent.txt", NULL));
}

/* ========== Process ========== */

typedef struct { Str out; Str err; } ProcCapture;

static void proc_stdout(const char *data, size_t len, void *ctx) {
    str_append_len(&((ProcCapture *)ctx)->out, data, len);
}
static void proc_stderr(const char *data, size_t len, void *ctx) {
    str_append_len(&((ProcCapture *)ctx)->err, data, len);
}

TEST(process_run_echo) {
    ProcCapture cap = { .out = str_new(64), .err = str_new(64) };
    ProcessOptions opts = {
        .command = "echo hello",
        .on_stdout = proc_stdout,
        .on_stderr = proc_stderr,
        .ctx = &cap,
    };
    ProcessResult result = {0};
    int rc = process_run(&opts, &result);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(result.exit_code, 0);
    ASSERT_TRUE(strstr(cap.out.data, "hello") != NULL);
    str_free(&cap.out);
    str_free(&cap.err);
}

TEST(process_run_exit_code) {
    ProcessOptions opts = { .command = "exit 42" };
    ProcessResult result = {0};
    int rc = process_run(&opts, &result);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(result.exit_code, 42);
}

TEST(process_run_stderr_output) {
    ProcCapture cap = { .out = str_new(64), .err = str_new(64) };
    ProcessOptions opts = {
        .command = "echo error >&2",
        .on_stdout = proc_stdout,
        .on_stderr = proc_stderr,
        .ctx = &cap,
    };
    ProcessResult result = {0};
    process_run(&opts, &result);
    ASSERT_TRUE(strstr(cap.err.data, "error") != NULL);
    str_free(&cap.out);
    str_free(&cap.err);
}

TEST(process_run_timeout) {
    ProcessOptions opts = {
        .command = "sleep 10",
        .timeout_ms = 100,
    };
    ProcessResult result = {0};
    process_run(&opts, &result);
    ASSERT_TRUE(result.timed_out);
}

TEST(process_run_cwd) {
    ProcCapture cap = { .out = str_new(64), .err = str_new(64) };
    ProcessOptions opts = {
        .command = "pwd",
        .cwd = "/tmp",
        .on_stdout = proc_stdout,
        .ctx = &cap,
    };
    ProcessResult result = {0};
    process_run(&opts, &result);
    ASSERT_TRUE(strstr(cap.out.data, "tmp") != NULL);
    str_free(&cap.out);
    str_free(&cap.err);
}

/* UNICODE: fs_write_file and fs_read_file with UTF-8 filename */
TEST(fs_write_read_utf8_filename) {
    const char *path = "/tmp/pi_test_\xE4\xB8\xAD\xE6\x96\x87.txt";
    ASSERT_EQ(fs_write_file(path, "data", 4), 0);
    size_t len = 0;
    char *data = fs_read_file(path, &len);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(len, 4);
    ASSERT_TRUE(memcmp(data, "data", 4) == 0);
    free(data);
    unlink(path);
}

/* UNICODE: fs_write_file with UTF-8 content */
TEST(fs_write_read_utf8_content) {
    const char *path = "/tmp/pi_test_utf8_content.txt";
    const char *content = "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80";
    size_t content_len = strlen(content);
    ASSERT_EQ(fs_write_file(path, content, content_len), 0);
    size_t len = 0;
    char *data = fs_read_file(path, &len);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(len, content_len);
    ASSERT_TRUE(memcmp(data, content, content_len) == 0);
    free(data);
    unlink(path);
}

/* UNICODE: fs_join with UTF-8 path components */
TEST(fs_join_utf8_components) {
    char *p = fs_join("/tmp", "\xE4\xB8\xAD\xE6\x96\x87");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p, "/tmp/\xE4\xB8\xAD\xE6\x96\x87");
    free(p);
}

/* RESOURCE EXHAUSTION: 10,000 character string via repeated append */
TEST(str_append_10k_chars) {
    Str s = str_new(64);
    for (int i = 0; i < 10000; i++) {
        str_append_char(&s, 'a');
    }
    ASSERT_EQ(s.len, 10000);
    ASSERT_EQ(s.data[0], 'a');
    ASSERT_EQ(s.data[9999], 'a');
    ASSERT_EQ(s.data[10000], '\0');
    str_free(&s);
}

/* RESOURCE EXHAUSTION: str_replace_all on string with 1000 matches */
TEST(str_replace_all_1000_matches) {
    Str s = str_new(2048);
    for (int i = 0; i < 1000; i++) {
        str_append(&s, "X.");
    }
    int count = str_replace_all(&s, "X", "YY");
    ASSERT_EQ(count, 1000);
    /* Original: "X.X.X...." (2000 chars), now "YY.YY.YY...." (3000 chars) */
    ASSERT_EQ(s.len, 3000);
    ASSERT_TRUE(s.data[0] == 'Y');
    str_free(&s);
}

/* RESOURCE EXHAUSTION: hashmap with 10,000 entries */
TEST(hashmap_10k_entries) {
    Hashmap *m = hashmap_create(16, NULL);
    char keys[10000][12];
    for (int i = 0; i < 10000; i++) {
        snprintf(keys[i], sizeof(keys[i]), "k%d", i);
        hashmap_set(m, keys[i], (void *)(long long)(i + 1));
    }
    ASSERT_EQ(hashmap_count(m), 10000);
    /* Verify all retrievable */
    for (int i = 0; i < 10000; i++) {
        ASSERT_EQ((long long)hashmap_get(m, keys[i]), i + 1);
    }
    hashmap_destroy(m);
}

/* RESOURCE EXHAUSTION: arena alloc until 1MB+ total */
TEST(arena_alloc_1mb) {
    Arena a = arena_create(4096);
    size_t total = 0;
    while (total < 1024 * 1024) {
        void *p = arena_alloc(&a, 1024);
        ASSERT_NOT_NULL(p);
        total += 1024;
    }
    ASSERT_TRUE(arena_total_allocated(&a) >= 1024 * 1024);
    arena_destroy(&a);
}

/* ========== ADVERSARIAL: SSE Parser Abuse ========== */

TEST(adv_sse_incomplete_event) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Feed data without double newline (event never dispatched) */
    const char *chunk = "data: incomplete";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 0);
    sse_parser_destroy(p);
}

TEST(adv_sse_huge_data_field) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Build a 1MB data line */
    size_t data_size = 1024 * 1024;
    char *big = malloc(data_size + 20);
    ASSERT_NOT_NULL(big);
    memcpy(big, "data: ", 6);
    memset(big + 6, 'X', data_size);
    big[6 + data_size] = '\n';
    big[6 + data_size + 1] = '\n';
    big[6 + data_size + 2] = '\0';
    sse_parser_feed(p, big, 6 + data_size + 2);
    ASSERT_EQ(ctx.count, 1);
    /* Data was dispatched, must not crash */
    free(big);
    sse_parser_destroy(p);
}

TEST(adv_sse_null_bytes) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Feed data with embedded null bytes */
    char chunk[] = "data: he\x00lo\n\n";
    /* Feed with known length including the null */
    sse_parser_feed(p, chunk, sizeof(chunk) - 1);
    /* Must not crash */
    sse_parser_destroy(p);
}

TEST(adv_sse_thousands_rapid_events) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Feed 5000 events rapidly */
    for (int i = 0; i < 5000; i++) {
        const char *chunk = "data: x\n\n";
        sse_parser_feed(p, chunk, strlen(chunk));
    }
    ASSERT_EQ(ctx.count, 5000);
    sse_parser_destroy(p);
}

TEST(adv_sse_malformed_event_type) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Event type with special characters */
    const char *chunk = "event: \xff\xfe\ndata: test\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    ASSERT_EQ(ctx.count, 1);
    sse_parser_destroy(p);
}

TEST(adv_sse_only_colons) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Lines with only colons are comments */
    const char *chunk = ":\n:\n:\n\n";
    sse_parser_feed(p, chunk, strlen(chunk));
    /* Comments plus empty line should dispatch empty event, but no data was set */
    /* Just verify no crash */
    sse_parser_destroy(p);
}

TEST(adv_sse_feed_null) {
    SSETestCtx ctx = {0};
    SSEParser *p = sse_parser_create(sse_test_cb, &ctx);
    /* Feed NULL chunk */
    sse_parser_feed(p, NULL, 0);
    /* Must not crash */
    sse_parser_destroy(p);
}

TEST(adv_sse_create_null_callback) {
    SSEParser *p = sse_parser_create(NULL, NULL);
    ASSERT_NULL(p);
}

TEST(process_run_abort) {
    volatile bool abort_flag = true;
    ProcessOptions opts = {
        .command = "sleep 10",
        .abort_flag = &abort_flag,
    };
    ProcessResult result = {0};
    process_run(&opts, &result);
    ASSERT_TRUE(result.aborted);
}

int main(void) {
    TEST_SUITE("Arena");
    RUN_TEST(arena_create_default);
    RUN_TEST(arena_create_custom);
    RUN_TEST(arena_alloc_basic);
    RUN_TEST(arena_alloc_zero);
    RUN_TEST(arena_alloc_large);
    RUN_TEST(arena_calloc_zeroed);
    RUN_TEST(arena_strdup_basic);
    RUN_TEST(arena_strdup_null);
    RUN_TEST(arena_strndup_basic);
    RUN_TEST(arena_strndup_longer);
    RUN_TEST(arena_sprintf_basic);
    RUN_TEST(arena_reset);
    RUN_TEST(arena_total_allocated);
    RUN_TEST(arena_multiple_blocks);

    TEST_SUITE("Str");
    RUN_TEST(str_new_default);
    RUN_TEST(str_from_basic);
    RUN_TEST(str_from_null);
    RUN_TEST(str_from_len_basic);
    RUN_TEST(str_from_len_zero);
    RUN_TEST(str_append_basic);
    RUN_TEST(str_append_null);
    RUN_TEST(str_append_len_basic);
    RUN_TEST(str_append_char_basic);
    RUN_TEST(str_appendf_basic);
    RUN_TEST(str_insert_begin);
    RUN_TEST(str_insert_middle);
    RUN_TEST(str_insert_end);
    RUN_TEST(str_insert_beyond_end);
    RUN_TEST(str_clear_basic);
    RUN_TEST(str_trim_both);
    RUN_TEST(str_trim_tabs_newlines);
    RUN_TEST(str_trim_all_whitespace);
    RUN_TEST(str_trim_empty);
    RUN_TEST(str_starts_with_true);
    RUN_TEST(str_starts_with_false);
    RUN_TEST(str_starts_with_longer);
    RUN_TEST(str_ends_with_true);
    RUN_TEST(str_ends_with_false);
    RUN_TEST(str_find_found);
    RUN_TEST(str_find_not_found);
    RUN_TEST(str_find_beginning);
    RUN_TEST(str_replace_basic);
    RUN_TEST(str_replace_not_found);
    RUN_TEST(str_replace_shorter);
    RUN_TEST(str_replace_longer);
    RUN_TEST(str_replace_all_basic);
    RUN_TEST(str_replace_all_growing);
    RUN_TEST(str_replace_all_shrinking);
    RUN_TEST(str_replace_all_none);
    RUN_TEST(str_replace_all_empty_pattern);
    RUN_TEST(str_take_basic);
    RUN_TEST(str_clone_basic);
    RUN_TEST(str_reserve_grows);
    RUN_TEST(str_free_null_safe);

    TEST_SUITE("UNICODE: Strings");
    RUN_TEST(str_append_utf8_multibyte);
    RUN_TEST(str_find_utf8_needle);
    RUN_TEST(str_replace_utf8);
    RUN_TEST(str_trim_utf8_whitespace);
    RUN_TEST(str_starts_with_utf8_prefix);

    TEST_SUITE("Hashmap");
    RUN_TEST(hashmap_create_destroy);
    RUN_TEST(hashmap_set_get);
    RUN_TEST(hashmap_get_missing);
    RUN_TEST(hashmap_has);
    RUN_TEST(hashmap_overwrite);
    RUN_TEST(hashmap_remove);
    RUN_TEST(hashmap_remove_missing);
    RUN_TEST(hashmap_many_entries);
    RUN_TEST(hashmap_iter);
    RUN_TEST(hashmap_free_value);
    RUN_TEST(hashmap_count_after_remove);

    TEST_SUITE("JSON");
    RUN_TEST(json_get_simple);
    RUN_TEST(json_get_nested);
    RUN_TEST(json_get_array);
    RUN_TEST(json_get_missing);
    RUN_TEST(json_get_null_root);
    RUN_TEST(json_get_string_basic);
    RUN_TEST(json_get_string_wrong_type);
    RUN_TEST(json_get_int_basic);
    RUN_TEST(json_get_int_default);
    RUN_TEST(json_get_double_basic);
    RUN_TEST(json_get_bool_true);
    RUN_TEST(json_get_bool_false);
    RUN_TEST(json_get_bool_default);
    RUN_TEST(json_set_string_basic);
    RUN_TEST(json_set_string_nested);
    RUN_TEST(json_set_int_basic);
    RUN_TEST(json_set_bool_basic);
    RUN_TEST(json_deep_merge_basic);
    RUN_TEST(json_deep_merge_nested);
    RUN_TEST(json_deep_merge_null_base);
    RUN_TEST(json_deep_merge_null_patch);
    RUN_TEST(json_deep_merge_both_null);
    RUN_TEST(json_read_write_file);
    RUN_TEST(json_read_file_missing);
    RUN_TEST(json_string_array_basic);
    RUN_TEST(json_string_array_null);

    TEST_SUITE("HTTP / SSE Parser");
    RUN_TEST(sse_parser_basic);
    RUN_TEST(sse_parser_typed_event);
    RUN_TEST(sse_parser_multi_data_lines);
    RUN_TEST(sse_parser_comment_ignored);
    RUN_TEST(sse_parser_multiple_events);
    RUN_TEST(sse_parser_chunked_input);
    RUN_TEST(sse_parser_destroy_null);
    RUN_TEST(http_response_free_null);

    TEST_SUITE("ADVERSARIAL: SSE Parser Abuse");
    RUN_TEST(adv_sse_incomplete_event);
    RUN_TEST(adv_sse_huge_data_field);
    RUN_TEST(adv_sse_null_bytes);
    RUN_TEST(adv_sse_thousands_rapid_events);
    RUN_TEST(adv_sse_malformed_event_type);
    RUN_TEST(adv_sse_only_colons);
    RUN_TEST(adv_sse_feed_null);
    RUN_TEST(adv_sse_create_null_callback);

    TEST_SUITE("FS");
    RUN_TEST(fs_write_read);
    RUN_TEST(fs_append_file);
    RUN_TEST(fs_exists_true);
    RUN_TEST(fs_exists_false);
    RUN_TEST(fs_is_dir_true);
    RUN_TEST(fs_is_dir_false);
    RUN_TEST(fs_is_file_true);
    RUN_TEST(fs_is_file_false);
    RUN_TEST(fs_mkdir_p_basic);
    RUN_TEST(fs_mtime_exists);
    RUN_TEST(fs_mtime_missing);
    RUN_TEST(fs_join_basic);
    RUN_TEST(fs_join_trailing_slash);
    RUN_TEST(fs_join_leading_slash);
    RUN_TEST(fs_join_null);
    RUN_TEST(fs_homedir_not_null);
    RUN_TEST(fs_expand_home_tilde);
    RUN_TEST(fs_expand_home_no_tilde);
    RUN_TEST(fs_expand_home_null);
    RUN_TEST(fs_read_file_missing);

    TEST_SUITE("UNICODE: File Operations");
    RUN_TEST(fs_write_read_utf8_filename);
    RUN_TEST(fs_write_read_utf8_content);
    RUN_TEST(fs_join_utf8_components);

    TEST_SUITE("RESOURCE EXHAUSTION: Strings");
    RUN_TEST(str_append_10k_chars);
    RUN_TEST(str_replace_all_1000_matches);

    TEST_SUITE("RESOURCE EXHAUSTION: Hashmap");
    RUN_TEST(hashmap_10k_entries);

    TEST_SUITE("RESOURCE EXHAUSTION: Arena");
    RUN_TEST(arena_alloc_1mb);

    TEST_SUITE("Process");
    RUN_TEST(process_run_echo);
    RUN_TEST(process_run_exit_code);
    RUN_TEST(process_run_stderr_output);
    RUN_TEST(process_run_timeout);
    RUN_TEST(process_run_cwd);
    RUN_TEST(process_run_abort);

    TEST_REPORT();
}
