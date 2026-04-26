/* test_agent.c — tests for src/agent/agent.c */
#include "test.h"
#include "agent/agent.h"
#include "ai/registry.h"
#include "ai/types.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>

/* ========== Message Queue ========== */

TEST(queue_init_basic) {
    MessageQueue q;
    queue_init(&q, QUEUE_ALL);
    ASSERT_FALSE(queue_has_items(&q));
    queue_free(&q);
}

TEST(queue_enqueue_drain_all) {
    MessageQueue q;
    queue_init(&q, QUEUE_ALL);
    queue_enqueue(&q, message_create_user("a"));
    queue_enqueue(&q, message_create_user("b"));
    ASSERT_TRUE(queue_has_items(&q));

    Message **out = NULL;
    int count = 0;
    queue_drain(&q, &out, &count);
    ASSERT_EQ(count, 2);
    ASSERT_NOT_NULL(out);
    for (int i = 0; i < count; i++) message_free(out[i]);
    free(out);
    ASSERT_FALSE(queue_has_items(&q));
    queue_free(&q);
}

TEST(queue_drain_one_at_a_time) {
    MessageQueue q;
    queue_init(&q, QUEUE_ONE_AT_A_TIME);
    queue_enqueue(&q, message_create_user("a"));
    queue_enqueue(&q, message_create_user("b"));

    Message **out = NULL;
    int count = 0;
    queue_drain(&q, &out, &count);
    ASSERT_EQ(count, 1);
    for (int i = 0; i < count; i++) message_free(out[i]);
    free(out);
    ASSERT_TRUE(queue_has_items(&q));

    queue_drain(&q, &out, &count);
    ASSERT_EQ(count, 1);
    for (int i = 0; i < count; i++) message_free(out[i]);
    free(out);
    ASSERT_FALSE(queue_has_items(&q));
    queue_free(&q);
}

TEST(queue_drain_empty) {
    MessageQueue q;
    queue_init(&q, QUEUE_ALL);
    Message **out = NULL;
    int count = 0;
    queue_drain(&q, &out, &count);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(out);
    queue_free(&q);
}

TEST(queue_clear) {
    MessageQueue q;
    queue_init(&q, QUEUE_ALL);
    queue_enqueue(&q, message_create_user("a"));
    queue_enqueue(&q, message_create_user("b"));
    queue_clear(&q);
    ASSERT_FALSE(queue_has_items(&q));
    queue_free(&q);
}

/* ========== Agent State ========== */

TEST(agent_state_create_free) {
    AgentState *s = agent_state_create();
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->message_count, 0);
    ASSERT_FALSE(s->is_streaming);
    ASSERT_FALSE(s->abort_requested);
    agent_state_free(s);
}

TEST(agent_state_add_message) {
    AgentState *s = agent_state_create();
    agent_state_add_message(s, message_create_user("hello"));
    ASSERT_EQ(s->message_count, 1);
    agent_state_add_message(s, message_create_assistant());
    ASSERT_EQ(s->message_count, 2);
    agent_state_free(s);
}

TEST(agent_state_reset) {
    AgentState *s = agent_state_create();
    agent_state_add_message(s, message_create_user("hello"));
    agent_state_reset(s);
    ASSERT_EQ(s->message_count, 0);
    ASSERT_FALSE(s->abort_requested);
    agent_state_free(s);
}

TEST(agent_state_free_null) {
    agent_state_free(NULL);
}

TEST(agent_abort) {
    AgentState *s = agent_state_create();
    ASSERT_FALSE(s->abort_requested);
    agent_abort(s);
    ASSERT_TRUE(s->abort_requested);
    agent_state_free(s);
}

TEST(agent_state_grow_capacity) {
    AgentState *s = agent_state_create();
    for (int i = 0; i < 50; i++) {
        agent_state_add_message(s, message_create_user("msg"));
    }
    ASSERT_EQ(s->message_count, 50);
    agent_state_free(s);
}

/* ========== Agent API NULL safety ========== */

TEST(agent_prompt_null_state) {
    ASSERT_EQ(agent_prompt(NULL, NULL, 0, NULL, NULL, NULL), -1);
}

TEST(agent_prompt_null_config) {
    AgentState *s = agent_state_create();
    ASSERT_EQ(agent_prompt(s, NULL, 0, NULL, NULL, NULL), -1);
    agent_state_free(s);
}

TEST(agent_continue_null) {
    ASSERT_EQ(agent_continue(NULL, NULL, NULL, NULL), -1);
}

static void dummy_cb(AgentEvent *e, void *u) { (void)e; (void)u; }

TEST(agent_continue_empty) {
    AgentState *s = agent_state_create();
    AgentLoopConfig cfg = {0};
    ASSERT_EQ(agent_continue(s, &cfg, dummy_cb, NULL), -1);
    agent_state_free(s);
}

TEST(agent_state_queues_independent) {
    AgentState *s = agent_state_create();
    queue_enqueue(&s->steering_queue, message_create_user("steer"));
    queue_enqueue(&s->follow_up_queue, message_create_user("follow"));
    ASSERT_TRUE(queue_has_items(&s->steering_queue));
    ASSERT_TRUE(queue_has_items(&s->follow_up_queue));
    agent_state_free(s);
}

TEST(agent_state_system_prompt) {
    AgentState *s = agent_state_create();
    s->system_prompt = strdup("You are helpful");
    ASSERT_STR_EQ(s->system_prompt, "You are helpful");
    agent_state_free(s);
}

int main(void) {
    TEST_SUITE("Message Queue");
    RUN_TEST(queue_init_basic);
    RUN_TEST(queue_enqueue_drain_all);
    RUN_TEST(queue_drain_one_at_a_time);
    RUN_TEST(queue_drain_empty);
    RUN_TEST(queue_clear);

    TEST_SUITE("Agent State");
    RUN_TEST(agent_state_create_free);
    RUN_TEST(agent_state_add_message);
    RUN_TEST(agent_state_reset);
    RUN_TEST(agent_state_free_null);
    RUN_TEST(agent_abort);
    RUN_TEST(agent_state_grow_capacity);

    TEST_SUITE("Agent API");
    RUN_TEST(agent_prompt_null_state);
    RUN_TEST(agent_prompt_null_config);
    RUN_TEST(agent_continue_null);
    RUN_TEST(agent_continue_empty);
    RUN_TEST(agent_state_queues_independent);
    RUN_TEST(agent_state_system_prompt);

    TEST_REPORT();
}
