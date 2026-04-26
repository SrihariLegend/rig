/* test_slash_commands.c — tests for slash command registry */
#include "test.h"
#include "harness/slash_commands.h"
#include <stdlib.h>
#include <string.h>

static int last_argc = -1;
static char last_args[256] = {0};

static int mock_handler(const char **args, int argc, void *ctx) {
    (void)ctx;
    last_argc = argc;
    last_args[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i > 0) strcat(last_args, " ");
        strcat(last_args, args[i]);
    }
    return 0;
}

static int fail_handler(const char **args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return 42;
}

/* ========== Registry ========== */

TEST(registry_create_free) {
    SlashCommandRegistry *reg = slash_registry_create();
    ASSERT_NOT_NULL(reg);
    ASSERT_EQ(reg->count, 0);
    slash_registry_free(reg);
}

TEST(register_command) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand cmd = {
        .name = "test", .description = "A test command",
        .usage = "/test", .handler = mock_handler,
        .ctx = NULL, .hidden = false
    };
    int rc = slash_register(reg, &cmd);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(reg->count, 1);
    slash_registry_free(reg);
}

TEST(register_duplicate_rejected) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand cmd = {
        .name = "dup", .description = "First",
        .handler = mock_handler
    };
    ASSERT_EQ(slash_register(reg, &cmd), 0);
    ASSERT_EQ(slash_register(reg, &cmd), -1);
    ASSERT_EQ(reg->count, 1);
    slash_registry_free(reg);
}

TEST(register_null_rejected) {
    SlashCommandRegistry *reg = slash_registry_create();
    ASSERT_EQ(slash_register(reg, NULL), -1);
    SlashCommand no_name = { .name = NULL, .handler = mock_handler };
    ASSERT_EQ(slash_register(reg, &no_name), -1);
    SlashCommand no_handler = { .name = "x", .handler = NULL };
    ASSERT_EQ(slash_register(reg, &no_handler), -1);
    slash_registry_free(reg);
}

/* ========== Execute ========== */

TEST(execute_simple) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand cmd = { .name = "ping", .handler = mock_handler };
    slash_register(reg, &cmd);

    int rc = slash_execute(reg, "/ping", NULL);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(last_argc, 0);
    slash_registry_free(reg);
}

TEST(execute_with_args) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand cmd = { .name = "greet", .handler = mock_handler };
    slash_register(reg, &cmd);

    int rc = slash_execute(reg, "/greet hello world", NULL);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(last_argc, 2);
    ASSERT_STR_EQ(last_args, "hello world");
    slash_registry_free(reg);
}

TEST(execute_unknown_command) {
    SlashCommandRegistry *reg = slash_registry_create();
    int rc = slash_execute(reg, "/nonexistent", NULL);
    ASSERT_EQ(rc, -1);
    slash_registry_free(reg);
}

TEST(execute_returns_handler_result) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand cmd = { .name = "fail", .handler = fail_handler };
    slash_register(reg, &cmd);

    int rc = slash_execute(reg, "/fail", NULL);
    ASSERT_EQ(rc, 42);
    slash_registry_free(reg);
}

TEST(execute_invalid_input) {
    SlashCommandRegistry *reg = slash_registry_create();
    ASSERT_EQ(slash_execute(reg, NULL, NULL), -1);
    ASSERT_EQ(slash_execute(reg, "", NULL), -1);
    ASSERT_EQ(slash_execute(reg, "no slash", NULL), -1);
    ASSERT_EQ(slash_execute(NULL, "/test", NULL), -1);
    slash_registry_free(reg);
}

/* ========== Complete ========== */

TEST(complete_all) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand c1 = { .name = "help", .handler = mock_handler };
    SlashCommand c2 = { .name = "hello", .handler = mock_handler };
    SlashCommand c3 = { .name = "quit", .handler = mock_handler };
    slash_register(reg, &c1);
    slash_register(reg, &c2);
    slash_register(reg, &c3);

    int count = 0;
    SlashCommand **matches = slash_complete(reg, "/hel", &count);
    ASSERT_EQ(count, 2);
    ASSERT_NOT_NULL(matches);
    free(matches);

    matches = slash_complete(reg, "/q", &count);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(matches[0]->name, "quit");
    free(matches);

    matches = slash_complete(reg, "/z", &count);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(matches);

    slash_registry_free(reg);
}

TEST(complete_hidden_excluded) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand visible = { .name = "show", .handler = mock_handler, .hidden = false };
    SlashCommand hidden = { .name = "secret", .handler = mock_handler, .hidden = true };
    slash_register(reg, &visible);
    slash_register(reg, &hidden);

    int count = 0;
    SlashCommand **matches = slash_complete(reg, "/s", &count);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(matches[0]->name, "show");
    free(matches);
    slash_registry_free(reg);
}

TEST(complete_empty_prefix) {
    SlashCommandRegistry *reg = slash_registry_create();
    SlashCommand c1 = { .name = "a", .handler = mock_handler };
    SlashCommand c2 = { .name = "b", .handler = mock_handler };
    slash_register(reg, &c1);
    slash_register(reg, &c2);

    int count = 0;
    SlashCommand **matches = slash_complete(reg, "", &count);
    ASSERT_EQ(count, 2);
    free(matches);
    slash_registry_free(reg);
}

/* ========== Builtins ========== */

TEST(builtins_registered) {
    SlashCommandRegistry *reg = slash_registry_create();
    slash_register_builtins(reg);
    ASSERT_TRUE(reg->count >= 7);

    /* Verify key builtins exist */
    int count = 0;
    SlashCommand **m = slash_complete(reg, "/help", &count);
    ASSERT_EQ(count, 1);
    free(m);

    m = slash_complete(reg, "/quit", &count);
    ASSERT_EQ(count, 1);
    free(m);

    m = slash_complete(reg, "/model", &count);
    ASSERT_EQ(count, 1);
    free(m);

    slash_registry_free(reg);
}

TEST(builtins_execute) {
    SlashCommandRegistry *reg = slash_registry_create();
    slash_register_builtins(reg);

    ASSERT_EQ(slash_execute(reg, "/quit", NULL), 0);
    ASSERT_EQ(slash_execute(reg, "/compact", NULL), 0);
    ASSERT_EQ(slash_execute(reg, "/session", NULL), 0);

    slash_registry_free(reg);
}

int main(void) {
    TEST_SUITE("Slash Commands: Registry");
    RUN_TEST(registry_create_free);
    RUN_TEST(register_command);
    RUN_TEST(register_duplicate_rejected);
    RUN_TEST(register_null_rejected);

    TEST_SUITE("Slash Commands: Execute");
    RUN_TEST(execute_simple);
    RUN_TEST(execute_with_args);
    RUN_TEST(execute_unknown_command);
    RUN_TEST(execute_returns_handler_result);
    RUN_TEST(execute_invalid_input);

    TEST_SUITE("Slash Commands: Complete");
    RUN_TEST(complete_all);
    RUN_TEST(complete_hidden_excluded);
    RUN_TEST(complete_empty_prefix);

    TEST_SUITE("Slash Commands: Builtins");
    RUN_TEST(builtins_registered);
    RUN_TEST(builtins_execute);

    TEST_REPORT();
}
