/* test_prompts.c — tests for src/harness/prompts.c */
#include "test.h"
#include "harness/prompts.h"
#include <stdlib.h>
#include <string.h>

TEST(expand_positional_1) {
    PromptTemplate pt = { .content = "Hello $1" };
    const char *args[] = {"World"};
    char *r = prompts_expand(&pt, args, 1);
    ASSERT_STR_EQ(r, "Hello World");
    free(r);
}

TEST(expand_positional_2) {
    PromptTemplate pt = { .content = "$1 and $2" };
    const char *args[] = {"A", "B"};
    char *r = prompts_expand(&pt, args, 2);
    ASSERT_STR_EQ(r, "A and B");
    free(r);
}

TEST(expand_positional_missing) {
    PromptTemplate pt = { .content = "val=$2" };
    const char *args[] = {"only"};
    char *r = prompts_expand(&pt, args, 1);
    ASSERT_STR_EQ(r, "val=");
    free(r);
}

TEST(expand_all_args) {
    PromptTemplate pt = { .content = "args: $@" };
    const char *args[] = {"a", "b", "c"};
    char *r = prompts_expand(&pt, args, 3);
    ASSERT_STR_EQ(r, "args: a b c");
    free(r);
}

TEST(expand_arguments_keyword) {
    PromptTemplate pt = { .content = "val=$ARGUMENTS" };
    const char *args[] = {"x", "y"};
    char *r = prompts_expand(&pt, args, 2);
    ASSERT_STR_EQ(r, "val=x y");
    free(r);
}

TEST(expand_range_from_n) {
    PromptTemplate pt = { .content = "rest: ${@:2}" };
    const char *args[] = {"a", "b", "c", "d"};
    char *r = prompts_expand(&pt, args, 4);
    ASSERT_STR_EQ(r, "rest: b c d");
    free(r);
}

TEST(expand_range_from_n_length) {
    PromptTemplate pt = { .content = "sub: ${@:2:2}" };
    const char *args[] = {"a", "b", "c", "d"};
    char *r = prompts_expand(&pt, args, 4);
    ASSERT_STR_EQ(r, "sub: b c");
    free(r);
}

TEST(expand_range_beyond_end) {
    PromptTemplate pt = { .content = "x: ${@:3:10}" };
    const char *args[] = {"a", "b", "c"};
    char *r = prompts_expand(&pt, args, 3);
    ASSERT_STR_EQ(r, "x: c");
    free(r);
}

TEST(expand_no_substitution) {
    PromptTemplate pt = { .content = "plain text" };
    char *r = prompts_expand(&pt, NULL, 0);
    ASSERT_STR_EQ(r, "plain text");
    free(r);
}

TEST(expand_dollar_literal) {
    PromptTemplate pt = { .content = "cost: $X" };
    char *r = prompts_expand(&pt, NULL, 0);
    ASSERT_STR_EQ(r, "cost: $X");
    free(r);
}

TEST(expand_null_template) {
    ASSERT_NULL(prompts_expand(NULL, NULL, 0));
}

TEST(expand_null_content) {
    PromptTemplate pt = { .content = NULL };
    ASSERT_NULL(prompts_expand(&pt, NULL, 0));
}

TEST(expand_all_args_empty) {
    PromptTemplate pt = { .content = "val=$@end" };
    char *r = prompts_expand(&pt, NULL, 0);
    ASSERT_STR_EQ(r, "val=end");
    free(r);
}

TEST(prompts_free_null) {
    prompts_free(NULL, 0);
}

/* UNICODE: template with UTF-8 characters */
TEST(expand_utf8_template) {
    PromptTemplate pt = { .content = "\xE4\xB8\xAD\xE6\x96\x87: $1" };
    const char *args[] = {"test"};
    char *r = prompts_expand(&pt, args, 1);
    ASSERT_STR_EQ(r, "\xE4\xB8\xAD\xE6\x96\x87: test");
    free(r);
}

/* UNICODE: arguments with UTF-8 */
TEST(expand_utf8_arguments) {
    PromptTemplate pt = { .content = "Hello $1 $2" };
    const char *args[] = {"\xF0\x9F\x98\x80", "\xE4\xB8\xAD\xE6\x96\x87"};
    char *r = prompts_expand(&pt, args, 2);
    ASSERT_STR_EQ(r, "Hello \xF0\x9F\x98\x80 \xE4\xB8\xAD\xE6\x96\x87");
    free(r);
}

int main(void) {
    TEST_SUITE("Prompts Expand");
    RUN_TEST(expand_positional_1);
    RUN_TEST(expand_positional_2);
    RUN_TEST(expand_positional_missing);
    RUN_TEST(expand_all_args);
    RUN_TEST(expand_arguments_keyword);
    RUN_TEST(expand_range_from_n);
    RUN_TEST(expand_range_from_n_length);
    RUN_TEST(expand_range_beyond_end);
    RUN_TEST(expand_no_substitution);
    RUN_TEST(expand_dollar_literal);
    RUN_TEST(expand_null_template);
    RUN_TEST(expand_null_content);
    RUN_TEST(expand_all_args_empty);
    RUN_TEST(prompts_free_null);

    TEST_SUITE("UNICODE: Prompts");
    RUN_TEST(expand_utf8_template);
    RUN_TEST(expand_utf8_arguments);

    TEST_REPORT();
}
