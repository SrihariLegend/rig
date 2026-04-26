/* test_compaction.c — tests for src/harness/compaction.c */
#include "test.h"
#include "harness/compaction.h"
#include "ai/types.h"
#include <stdlib.h>
#include <string.h>

/* ========== Token Estimation ========== */

TEST(estimate_null_message) {
    ASSERT_EQ(estimate_tokens_message(NULL), 0);
}

TEST(estimate_empty_message) {
    Message m = { .role = ROLE_USER, .content = NULL, .content_count = 0 };
    /* Should just return the overhead */
    int tokens = estimate_tokens_message(&m);
    ASSERT_EQ(tokens, 4);
}

TEST(estimate_text_message) {
    /* "hello world" = 11 chars / 4 = ~3 tokens + 4 overhead = 7 */
    Message *m = message_create_user("hello world");
    int tokens = estimate_tokens_message(m);
    ASSERT_TRUE(tokens > 0);
    ASSERT_TRUE(tokens < 20);
    message_free(m);
}

TEST(estimate_long_message) {
    /* 4000 chars -> ~1000 tokens + overhead */
    char *long_text = malloc(4001);
    memset(long_text, 'A', 4000);
    long_text[4000] = '\0';
    Message *m = message_create_user(long_text);
    int tokens = estimate_tokens_message(m);
    ASSERT_TRUE(tokens >= 1000);
    ASSERT_TRUE(tokens <= 1010);
    free(long_text);
    message_free(m);
}

TEST(estimate_messages_array) {
    Message *m1 = message_create_user("hello");
    Message *m2 = message_create_user("world");
    Message *msgs[] = { m1, m2 };
    int total = estimate_tokens_messages(msgs, 2);
    ASSERT_TRUE(total > 0);
    int individual = estimate_tokens_message(m1) + estimate_tokens_message(m2);
    ASSERT_EQ(total, individual);
    message_free(m1);
    message_free(m2);
}

TEST(estimate_messages_null) {
    ASSERT_EQ(estimate_tokens_messages(NULL, 0), 0);
    ASSERT_EQ(estimate_tokens_messages(NULL, 5), 0);
}

/* ========== Default config ========== */

TEST(config_defaults) {
    CompactionConfig cfg = compaction_config_default();
    ASSERT_TRUE(cfg.enabled);
    ASSERT_EQ(cfg.reserve_tokens, 16384);
    ASSERT_EQ(cfg.keep_recent_tokens, 20000);
}

/* ========== needs_compaction ========== */

TEST(needs_compaction_small) {
    /* Small conversation should not need compaction */
    Message *m1 = message_create_user("hello");
    Message *msgs[] = { m1 };
    CompactionConfig cfg = compaction_config_default();
    bool needed = needs_compaction(msgs, 1, 200000, &cfg);
    ASSERT_FALSE(needed);
    message_free(m1);
}

TEST(needs_compaction_large) {
    /* Create messages that exceed context_window - reserve_tokens */
    int count = 100;
    Message **msgs = calloc((size_t)count, sizeof(Message *));
    char *big_text = malloc(8001);
    memset(big_text, 'X', 8000);
    big_text[8000] = '\0';
    for (int i = 0; i < count; i++) {
        msgs[i] = message_create_user(big_text);
    }
    free(big_text);

    /* 100 msgs * ~2000 tokens each = ~200000 tokens */
    CompactionConfig cfg = compaction_config_default();
    /* context_window=50000, reserve=16384 => threshold=33616 */
    bool needed = needs_compaction(msgs, count, 50000, &cfg);
    ASSERT_TRUE(needed);

    for (int i = 0; i < count; i++) message_free(msgs[i]);
    free(msgs);
}

TEST(needs_compaction_disabled) {
    Message *m1 = message_create_user("hello");
    Message *msgs[] = { m1 };
    CompactionConfig cfg = compaction_config_default();
    cfg.enabled = false;
    ASSERT_FALSE(needs_compaction(msgs, 1, 100, &cfg));
    message_free(m1);
}

TEST(needs_compaction_null) {
    CompactionConfig cfg = compaction_config_default();
    ASSERT_FALSE(needs_compaction(NULL, 0, 200000, &cfg));
    ASSERT_FALSE(needs_compaction(NULL, 5, 200000, &cfg));
}

/* ========== compact_messages ========== */

TEST(compact_small_noop) {
    /* Small conversation: compaction should be a no-op (clone all) */
    Message *m1 = message_create_user("system prompt");
    Message *m2 = message_create_user("hello");
    Message *m3 = message_create_user("world");
    Message *msgs[] = { m1, m2, m3 };

    CompactionConfig cfg = compaction_config_default();
    Message **out = NULL;
    int out_count = 0;

    int ret = compact_messages(msgs, 3, 200000, &cfg, &out, &out_count);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(out_count, 3);
    ASSERT_NOT_NULL(out);

    /* Verify cloned, not same pointer */
    ASSERT_TRUE(out[0] != m1);
    ASSERT_TRUE(out[1] != m2);
    ASSERT_TRUE(out[2] != m3);

    for (int i = 0; i < out_count; i++) message_free(out[i]);
    free(out);
    message_free(m1);
    message_free(m2);
    message_free(m3);
}

TEST(compact_large_drops_middle) {
    /* Create a conversation that exceeds budget */
    int count = 20;
    Message **msgs = calloc((size_t)count, sizeof(Message *));

    /* First message: system prompt (small) */
    msgs[0] = message_create_user("You are a helpful assistant.");

    /* Middle messages: large */
    char *big = malloc(2001);
    memset(big, 'M', 2000);
    big[2000] = '\0';
    for (int i = 1; i < count - 2; i++) {
        msgs[i] = message_create_user(big);
    }
    free(big);

    /* Last 2 messages: recent */
    msgs[count - 2] = message_create_user("recent question");
    msgs[count - 1] = message_create_user("latest question");

    CompactionConfig cfg = {
        .enabled = true,
        .reserve_tokens = 100,
        .keep_recent_tokens = 500,
    };

    Message **out = NULL;
    int out_count = 0;

    /* Small context window forces compaction */
    int ret = compact_messages(msgs, count, 600, &cfg, &out, &out_count);
    ASSERT_EQ(ret, 0);
    ASSERT_NOT_NULL(out);

    /* Should keep first + some recent, dropping middle */
    ASSERT_TRUE(out_count < count);
    ASSERT_TRUE(out_count >= 2); /* at least first + 1 recent */

    /* First message should be the system prompt */
    ASSERT_TRUE(out[0]->content_count > 0);
    ASSERT_STR_EQ(out[0]->content[0].text.text, "You are a helpful assistant.");

    /* Last message should be the latest */
    ASSERT_STR_EQ(out[out_count - 1]->content[0].text.text, "latest question");

    for (int i = 0; i < out_count; i++) message_free(out[i]);
    free(out);
    for (int i = 0; i < count; i++) message_free(msgs[i]);
    free(msgs);
}

TEST(compact_preserves_recent) {
    /* Even with heavy compaction, recent messages should be preserved */
    Message *m0 = message_create_user("system");
    Message *m1 = message_create_user("old message 1");
    Message *m2 = message_create_user("old message 2");
    Message *m3 = message_create_user("recent 1");
    Message *m4 = message_create_user("recent 2");
    Message *msgs[] = { m0, m1, m2, m3, m4 };

    CompactionConfig cfg = {
        .enabled = true,
        .reserve_tokens = 5,
        .keep_recent_tokens = 50,
    };

    Message **out = NULL;
    int out_count = 0;

    /* Tight budget: 20 tokens total (= 15 usable) */
    int ret = compact_messages(msgs, 5, 20, &cfg, &out, &out_count);
    ASSERT_EQ(ret, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_TRUE(out_count >= 2); /* at least system + 1 recent */

    /* Last output should be "recent 2" */
    ASSERT_STR_EQ(out[out_count - 1]->content[0].text.text, "recent 2");

    for (int i = 0; i < out_count; i++) message_free(out[i]);
    free(out);
    message_free(m0);
    message_free(m1);
    message_free(m2);
    message_free(m3);
    message_free(m4);
}

TEST(compact_empty) {
    Message **out = NULL;
    int out_count = 0;
    CompactionConfig cfg = compaction_config_default();
    int ret = compact_messages(NULL, 0, 200000, &cfg, &out, &out_count);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(out_count, 0);
}

TEST(compact_null_args) {
    ASSERT_EQ(compact_messages(NULL, 0, 200000, NULL, NULL, NULL), -1);
}

TEST(compact_single_message) {
    Message *m = message_create_user("only message");
    Message *msgs[] = { m };

    CompactionConfig cfg = compaction_config_default();
    Message **out = NULL;
    int out_count = 0;

    int ret = compact_messages(msgs, 1, 200000, &cfg, &out, &out_count);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(out_count, 1);
    ASSERT_STR_EQ(out[0]->content[0].text.text, "only message");

    message_free(out[0]);
    free(out);
    message_free(m);
}

int main(void) {
    TEST_SUITE("Token Estimation");
    RUN_TEST(estimate_null_message);
    RUN_TEST(estimate_empty_message);
    RUN_TEST(estimate_text_message);
    RUN_TEST(estimate_long_message);
    RUN_TEST(estimate_messages_array);
    RUN_TEST(estimate_messages_null);

    TEST_SUITE("Config");
    RUN_TEST(config_defaults);

    TEST_SUITE("Needs Compaction");
    RUN_TEST(needs_compaction_small);
    RUN_TEST(needs_compaction_large);
    RUN_TEST(needs_compaction_disabled);
    RUN_TEST(needs_compaction_null);

    TEST_SUITE("Compact Messages");
    RUN_TEST(compact_small_noop);
    RUN_TEST(compact_large_drops_middle);
    RUN_TEST(compact_preserves_recent);
    RUN_TEST(compact_empty);
    RUN_TEST(compact_null_args);
    RUN_TEST(compact_single_message);

    TEST_REPORT();
}
