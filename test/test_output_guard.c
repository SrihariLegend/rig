/* test_output_guard.c — tests for output truncation guard */
#include "test.h"
#include "harness/output_guard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========== Defaults ========== */

TEST(defaults_sane) {
    OutputGuardConfig cfg = output_guard_defaults();
    ASSERT_EQ(cfg.max_output_bytes, 512 * 1024);
    ASSERT_EQ(cfg.max_output_lines, 10000);
    ASSERT_EQ(cfg.truncation_lines, 50);
    ASSERT_TRUE(cfg.warn_on_truncation);
}

/* ========== Small output (no-op) ========== */

TEST(small_output_passthrough) {
    const char *input = "hello world\n";
    GuardedOutput *go = output_guard_apply(input, (int)strlen(input), NULL);
    ASSERT_NOT_NULL(go);
    ASSERT_FALSE(go->was_truncated);
    ASSERT_STR_EQ(go->content, input);
    ASSERT_EQ(go->content_len, (int)strlen(input));
    ASSERT_EQ(go->original_bytes, (int)strlen(input));
    ASSERT_EQ(go->original_lines, 1);
    guarded_output_free(go);
}

TEST(multiline_small) {
    const char *input = "line1\nline2\nline3\n";
    GuardedOutput *go = output_guard_apply(input, (int)strlen(input), NULL);
    ASSERT_NOT_NULL(go);
    ASSERT_FALSE(go->was_truncated);
    ASSERT_EQ(go->original_lines, 3);
    guarded_output_free(go);
}

/* ========== Empty input ========== */

TEST(empty_input) {
    GuardedOutput *go = output_guard_apply("", 0, NULL);
    ASSERT_NOT_NULL(go);
    ASSERT_FALSE(go->was_truncated);
    ASSERT_EQ(go->content_len, 0);
    ASSERT_EQ(go->original_bytes, 0);
    ASSERT_EQ(go->original_lines, 0);
    guarded_output_free(go);
}

TEST(null_input) {
    GuardedOutput *go = output_guard_apply(NULL, 0, NULL);
    ASSERT_NOT_NULL(go);
    ASSERT_FALSE(go->was_truncated);
    ASSERT_EQ(go->content_len, 0);
    guarded_output_free(go);
}

/* ========== Line limit truncation ========== */

TEST(line_limit_truncation) {
    /* Build output with 200 lines, limit to 50 */
    char buf[4096];
    buf[0] = '\0';
    for (int i = 0; i < 200; i++) {
        char line[32];
        snprintf(line, sizeof(line), "line %d\n", i);
        strcat(buf, line);
    }

    OutputGuardConfig cfg = {
        .max_output_bytes = 1024 * 1024,
        .max_output_lines = 50,
        .truncation_lines = 5,
        .warn_on_truncation = true,
    };

    GuardedOutput *go = output_guard_apply(buf, (int)strlen(buf), &cfg);
    ASSERT_NOT_NULL(go);
    ASSERT_TRUE(go->was_truncated);
    ASSERT_EQ(go->original_lines, 200);
    ASSERT_TRUE(go->content_len < (int)strlen(buf));

    /* Check truncation banner is present */
    ASSERT_TRUE(strstr(go->content, "truncated") != NULL);

    /* Check first lines are present */
    ASSERT_TRUE(strstr(go->content, "line 0") != NULL);
    ASSERT_TRUE(strstr(go->content, "line 4") != NULL);

    /* Check last lines are present */
    ASSERT_TRUE(strstr(go->content, "line 199") != NULL);
    ASSERT_TRUE(strstr(go->content, "line 195") != NULL);

    guarded_output_free(go);
}

/* ========== Byte limit truncation ========== */

TEST(byte_limit_truncation) {
    /* Create output that exceeds byte limit */
    int size = 2000;
    char *big = malloc((size_t)size + 1);
    for (int i = 0; i < size; i++) {
        big[i] = (i % 80 == 79) ? '\n' : 'x';
    }
    big[size] = '\0';

    OutputGuardConfig cfg = {
        .max_output_bytes = 500,
        .max_output_lines = 100000,
        .truncation_lines = 3,
        .warn_on_truncation = true,
    };

    GuardedOutput *go = output_guard_apply(big, size, &cfg);
    ASSERT_NOT_NULL(go);
    ASSERT_TRUE(go->was_truncated);
    ASSERT_EQ(go->original_bytes, size);
    ASSERT_TRUE(go->content_len < size);
    ASSERT_TRUE(strstr(go->content, "truncated") != NULL);

    guarded_output_free(go);
    free(big);
}

/* ========== No trailing newline ========== */

TEST(no_trailing_newline) {
    const char *input = "no newline at end";
    GuardedOutput *go = output_guard_apply(input, (int)strlen(input), NULL);
    ASSERT_NOT_NULL(go);
    ASSERT_FALSE(go->was_truncated);
    ASSERT_EQ(go->original_lines, 1);
    guarded_output_free(go);
}

/* ========== Exactly at limit ========== */

TEST(exactly_at_line_limit) {
    char buf[1024];
    buf[0] = '\0';
    for (int i = 0; i < 10; i++) {
        char line[32];
        snprintf(line, sizeof(line), "line %d\n", i);
        strcat(buf, line);
    }

    OutputGuardConfig cfg = {
        .max_output_bytes = 1024 * 1024,
        .max_output_lines = 10,
        .truncation_lines = 3,
        .warn_on_truncation = true,
    };

    GuardedOutput *go = output_guard_apply(buf, (int)strlen(buf), &cfg);
    ASSERT_NOT_NULL(go);
    ASSERT_FALSE(go->was_truncated);
    guarded_output_free(go);
}

int main(void) {
    TEST_SUITE("Output Guard: Defaults");
    RUN_TEST(defaults_sane);

    TEST_SUITE("Output Guard: Small Output");
    RUN_TEST(small_output_passthrough);
    RUN_TEST(multiline_small);

    TEST_SUITE("Output Guard: Empty/Null Input");
    RUN_TEST(empty_input);
    RUN_TEST(null_input);

    TEST_SUITE("Output Guard: Truncation");
    RUN_TEST(line_limit_truncation);
    RUN_TEST(byte_limit_truncation);

    TEST_SUITE("Output Guard: Edge Cases");
    RUN_TEST(no_trailing_newline);
    RUN_TEST(exactly_at_line_limit);

    TEST_REPORT();
}
