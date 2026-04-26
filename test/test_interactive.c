/* test_interactive.c — tests for interactive mode state and components */
#include "test.h"
#include "tui/tui.h"
#include "tui/widgets/text.h"
#include "tui/widgets/input.h"
#include "tui/widgets/loader.h"
#include "agent/agent.h"
#include "ai/types.h"
#include "harness/session.h"
#include "util/str.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

/* ========== Helper: Simulated Interactive State ========== */

/*
 * We test the interactive mode's components in isolation rather than
 * calling interactive_mode_start (which needs a terminal). This mirrors
 * how the interactive state manages its widgets, agent, and session.
 */

typedef struct {
    TUI *tui;
    Component *history[64];
    int history_count;
    Component *status_line;
    Component *loader;
    Component *input;
    AgentState *agent;
    Session *session;
    Str current_text;
    Component *current_widget;
} TestInteractiveState;

static TestInteractiveState *test_state_create(void) {
    TestInteractiveState *s = calloc(1, sizeof(TestInteractiveState));
    if (!s) return NULL;

    s->tui = tui_create();
    s->status_line = widget_text_create("");
    s->loader = widget_loader_create("Thinking...");
    s->input = widget_input_create("Type a message...");
    s->agent = agent_state_create();
    s->current_text = str_new(256);

    return s;
}

static void test_state_free(TestInteractiveState *s) {
    if (!s) return;

    for (int i = 0; i < s->history_count; i++) {
        component_free(s->history[i]);
    }
    if (s->status_line) component_free(s->status_line);
    if (s->loader) component_free(s->loader);
    if (s->input) component_free(s->input);
    if (s->agent) agent_state_free(s->agent);
    if (s->session) session_free(s->session);
    str_free(&s->current_text);
    tui_free(s->tui);
    free(s);
}

static void test_state_add_history(TestInteractiveState *s, const char *prefix, const char *text) {
    if (s->history_count >= 64) return;

    Str formatted = str_new(256);
    if (prefix) {
        str_appendf(&formatted, "%s %s", prefix, text ? text : "");
    } else {
        str_append(&formatted, text ? text : "");
    }

    Component *widget = widget_text_create(formatted.data);
    str_free(&formatted);

    s->history[s->history_count++] = widget;
}

static void test_state_rebuild(TestInteractiveState *s) {
    while (s->tui->component_count > 0) {
        tui_remove_component(s->tui, s->tui->components[0]);
    }

    for (int i = 0; i < s->history_count; i++) {
        tui_add_component(s->tui, s->history[i]);
    }

    tui_add_component(s->tui, s->status_line);
    tui_add_component(s->tui, s->input);
}

/* ========== Tests: State Creation ========== */

TEST(interactive_state_creates_components) {
    TestInteractiveState *s = test_state_create();
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->tui);
    ASSERT_NOT_NULL(s->status_line);
    ASSERT_NOT_NULL(s->loader);
    ASSERT_NOT_NULL(s->input);
    ASSERT_NOT_NULL(s->agent);
    ASSERT_EQ(s->history_count, 0);
    test_state_free(s);
}

TEST(interactive_input_widget_has_placeholder) {
    TestInteractiveState *s = test_state_create();

    /* Input should render with placeholder when empty */
    int lines = 0;
    char **out = s->input->render(s->input, 80, &lines);
    ASSERT_EQ(lines, 1);
    ASSERT_NOT_NULL(out[0]);
    /* Placeholder is rendered with dim ANSI, check it contains the text */
    ASSERT_TRUE(strstr(out[0], "Type a message...") != NULL);

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    test_state_free(s);
}

TEST(interactive_input_focused) {
    TestInteractiveState *s = test_state_create();
    ASSERT_TRUE(s->input->focused);
    test_state_free(s);
}

/* ========== Tests: Adding User Message ========== */

TEST(interactive_add_user_message) {
    TestInteractiveState *s = test_state_create();

    test_state_add_history(s, ">", "Hello, world!");
    ASSERT_EQ(s->history_count, 1);

    /* Verify text renders correctly */
    int lines = 0;
    char **out = s->history[0]->render(s->history[0], 80, &lines);
    ASSERT_TRUE(lines >= 1);
    ASSERT_NOT_NULL(out[0]);
    ASSERT_TRUE(strstr(out[0], "Hello, world!") != NULL);

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    test_state_free(s);
}

TEST(interactive_add_multiple_messages) {
    TestInteractiveState *s = test_state_create();

    test_state_add_history(s, ">", "First message");
    test_state_add_history(s, "<", "Assistant response");
    test_state_add_history(s, ">", "Second message");

    ASSERT_EQ(s->history_count, 3);

    /* Check each message contains expected content */
    int lines = 0;
    char **out = s->history[0]->render(s->history[0], 80, &lines);
    ASSERT_TRUE(strstr(out[0], "First message") != NULL);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);

    out = s->history[1]->render(s->history[1], 80, &lines);
    ASSERT_TRUE(strstr(out[0], "Assistant response") != NULL);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);

    out = s->history[2]->render(s->history[2], 80, &lines);
    ASSERT_TRUE(strstr(out[0], "Second message") != NULL);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);

    test_state_free(s);
}

/* ========== Tests: TUI Component Layout ========== */

TEST(interactive_rebuild_tui_layout) {
    TestInteractiveState *s = test_state_create();

    test_state_add_history(s, ">", "Hello");
    test_state_rebuild(s);

    /* Should have: 1 history + status_line + input = 3 components */
    ASSERT_EQ(s->tui->component_count, 3);

    test_state_free(s);
}

TEST(interactive_rebuild_with_multiple_history) {
    TestInteractiveState *s = test_state_create();

    test_state_add_history(s, ">", "msg1");
    test_state_add_history(s, "<", "resp1");
    test_state_add_history(s, ">", "msg2");
    test_state_rebuild(s);

    /* 3 history + status_line + input = 5 */
    ASSERT_EQ(s->tui->component_count, 5);

    test_state_free(s);
}

/* ========== Tests: Stream Callback Simulation ========== */

TEST(interactive_stream_text_delta) {
    TestInteractiveState *s = test_state_create();

    /* Simulate starting an assistant message */
    Component *assistant_widget = widget_text_create("");
    s->history[s->history_count++] = assistant_widget;
    s->current_widget = assistant_widget;
    s->current_text = str_new(256);

    /* Simulate text delta events */
    str_append(&s->current_text, "Hello");
    widget_text_set(s->current_widget, s->current_text.data);

    str_append(&s->current_text, " world");
    widget_text_set(s->current_widget, s->current_text.data);

    /* Verify final accumulated text */
    int lines = 0;
    char **out = assistant_widget->render(assistant_widget, 80, &lines);
    ASSERT_TRUE(lines >= 1);
    ASSERT_STR_EQ(out[0], "Hello world");

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    test_state_free(s);
}

TEST(interactive_stream_multiple_deltas) {
    TestInteractiveState *s = test_state_create();

    Component *widget = widget_text_create("");
    s->history[s->history_count++] = widget;
    s->current_widget = widget;

    Str text = str_new(256);
    const char *deltas[] = {"The ", "quick ", "brown ", "fox"};
    for (int i = 0; i < 4; i++) {
        str_append(&text, deltas[i]);
        widget_text_set(widget, text.data);
    }

    int lines = 0;
    char **out = widget->render(widget, 80, &lines);
    ASSERT_TRUE(lines >= 1);
    ASSERT_STR_EQ(out[0], "The quick brown fox");

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    str_free(&text);
    test_state_free(s);
}

/* ========== Tests: Status Line ========== */

TEST(interactive_status_line_update) {
    TestInteractiveState *s = test_state_create();

    widget_text_set(s->status_line, "claude-sonnet-4-6 | 150 tokens");

    int lines = 0;
    char **out = s->status_line->render(s->status_line, 80, &lines);
    ASSERT_EQ(lines, 1);
    ASSERT_TRUE(strstr(out[0], "claude-sonnet-4-6") != NULL);
    ASSERT_TRUE(strstr(out[0], "150 tokens") != NULL);

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    test_state_free(s);
}

TEST(interactive_status_line_thinking_indicator) {
    TestInteractiveState *s = test_state_create();

    widget_text_set(s->status_line, "model | thinking...");

    int lines = 0;
    char **out = s->status_line->render(s->status_line, 80, &lines);
    ASSERT_TRUE(strstr(out[0], "thinking...") != NULL);

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    test_state_free(s);
}

/* ========== Tests: Loader Widget ========== */

TEST(interactive_loader_displays_thinking) {
    TestInteractiveState *s = test_state_create();

    int lines = 0;
    char **out = s->loader->render(s->loader, 80, &lines);
    ASSERT_EQ(lines, 1);
    ASSERT_TRUE(strstr(out[0], "Thinking...") != NULL);

    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    test_state_free(s);
}

TEST(interactive_loader_tick_updates) {
    TestInteractiveState *s = test_state_create();

    /* Get first frame */
    int lines1 = 0;
    char **out1 = s->loader->render(s->loader, 80, &lines1);
    char *frame1 = strdup(out1[0]);
    for (int i = 0; i < lines1; i++) free(out1[i]);
    free(out1);

    /* Tick and get second frame */
    widget_loader_tick(s->loader);
    int lines2 = 0;
    char **out2 = s->loader->render(s->loader, 80, &lines2);

    /* Frames should be different (different spinner character) */
    ASSERT_TRUE(strcmp(frame1, out2[0]) != 0);

    free(frame1);
    for (int i = 0; i < lines2; i++) free(out2[i]);
    free(out2);
    test_state_free(s);
}

/* ========== Tests: Input Capture and Clear ========== */

TEST(interactive_input_capture_and_clear) {
    TestInteractiveState *s = test_state_create();

    /* Simulate typing */
    s->input->handle_input(s->input, "H", 1);
    s->input->handle_input(s->input, "e", 1);
    s->input->handle_input(s->input, "l", 1);
    s->input->handle_input(s->input, "l", 1);
    s->input->handle_input(s->input, "o", 1);

    ASSERT_STR_EQ(widget_input_get_text(s->input), "Hello");

    /* Capture text before clearing */
    char *captured = strdup(widget_input_get_text(s->input));
    widget_input_clear(s->input);

    ASSERT_STR_EQ(captured, "Hello");
    ASSERT_STR_EQ(widget_input_get_text(s->input), "");

    free(captured);
    test_state_free(s);
}

/* ========== Tests: Session Integration ========== */

TEST(interactive_session_create) {
    /* Create a temp directory for session files */
    char tmpdir[] = "/tmp/pi_test_session_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));

    Session *session = session_create(tmpdir);
    ASSERT_NOT_NULL(session);
    ASSERT_NOT_NULL(session->session_id);

    session_free(session);

    /* Cleanup */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

TEST(interactive_session_append_messages) {
    char tmpdir[] = "/tmp/pi_test_session_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));

    Session *session = session_create(tmpdir);
    ASSERT_NOT_NULL(session);

    /* Append a user message */
    cJSON *user_data = cJSON_CreateObject();
    cJSON_AddStringToObject(user_data, "role", "user");
    cJSON_AddStringToObject(user_data, "text", "Hello");
    int rc = session_append(session, ENTRY_MESSAGE, user_data);
    ASSERT_EQ(rc, 0);

    /* Append an assistant message */
    cJSON *asst_data = cJSON_CreateObject();
    cJSON_AddStringToObject(asst_data, "role", "assistant");
    cJSON_AddStringToObject(asst_data, "text", "Hi there!");
    rc = session_append(session, ENTRY_MESSAGE, asst_data);
    ASSERT_EQ(rc, 0);

    /* Verify entries were added */
    ASSERT_EQ(session->entry_count, 2);

    /* Flush to disk */
    rc = session_flush(session);
    ASSERT_EQ(rc, 0);

    session_free(session);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

TEST(interactive_session_persist_and_reload) {
    char tmpdir[] = "/tmp/pi_test_session_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));

    /* Create session and add entries */
    Session *session = session_create(tmpdir);
    ASSERT_NOT_NULL(session);
    char *session_path = strdup(session->file_path);

    cJSON *data1 = cJSON_CreateObject();
    cJSON_AddStringToObject(data1, "role", "user");
    cJSON_AddStringToObject(data1, "text", "Test message");
    session_append(session, ENTRY_MESSAGE, data1);
    session_flush(session);

    session_free(session);

    /* Reload the session */
    Session *reloaded = session_load(session_path);
    ASSERT_NOT_NULL(reloaded);
    ASSERT_TRUE(reloaded->entry_count >= 1);

    session_free(reloaded);
    free(session_path);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
}

/* ========== Tests: Agent State Integration ========== */

TEST(interactive_agent_state_add_messages) {
    AgentState *agent = agent_state_create();
    ASSERT_NOT_NULL(agent);

    Message *user_msg = message_create_user("Hello");
    agent_state_add_message(agent, user_msg);
    ASSERT_EQ(agent->message_count, 1);

    Message *asst_msg = message_create_assistant();
    message_add_content(asst_msg, content_text("Hi there!", NULL));
    agent_state_add_message(agent, asst_msg);
    ASSERT_EQ(agent->message_count, 2);

    agent_state_free(agent);
}

/* ========== Tests: Full Flow Simulation ========== */

TEST(interactive_full_flow_user_to_display) {
    TestInteractiveState *s = test_state_create();

    /* 1. User types a message */
    widget_input_set_text(s->input, "What is 2+2?");
    ASSERT_STR_EQ(widget_input_get_text(s->input), "What is 2+2?");

    /* 2. Capture and clear input (simulating Enter press) */
    char *prompt = strdup(widget_input_get_text(s->input));
    widget_input_clear(s->input);

    /* 3. Add user message to history display */
    test_state_add_history(s, ">", prompt);
    ASSERT_EQ(s->history_count, 1);

    /* 4. Simulate streaming response */
    Component *asst_widget = widget_text_create("");
    s->history[s->history_count++] = asst_widget;

    Str response = str_new(256);
    str_append(&response, "2+2 equals 4");
    widget_text_set(asst_widget, response.data);

    /* 5. Verify the complete conversation is displayed */
    test_state_rebuild(s);
    /* 2 history + status + input = 4 */
    ASSERT_EQ(s->tui->component_count, 4);

    /* Verify content */
    int lines = 0;
    char **out = s->history[0]->render(s->history[0], 80, &lines);
    ASSERT_TRUE(strstr(out[0], "What is 2+2?") != NULL);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);

    out = s->history[1]->render(s->history[1], 80, &lines);
    ASSERT_STR_EQ(out[0], "2+2 equals 4");
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);

    str_free(&response);
    free(prompt);
    test_state_free(s);
}

/* ========== Main ========== */

int main(void) {
    TEST_SUITE("Interactive: State Creation");
    RUN_TEST(interactive_state_creates_components);
    RUN_TEST(interactive_input_widget_has_placeholder);
    RUN_TEST(interactive_input_focused);

    TEST_SUITE("Interactive: User Messages");
    RUN_TEST(interactive_add_user_message);
    RUN_TEST(interactive_add_multiple_messages);

    TEST_SUITE("Interactive: TUI Layout");
    RUN_TEST(interactive_rebuild_tui_layout);
    RUN_TEST(interactive_rebuild_with_multiple_history);

    TEST_SUITE("Interactive: Stream Display");
    RUN_TEST(interactive_stream_text_delta);
    RUN_TEST(interactive_stream_multiple_deltas);

    TEST_SUITE("Interactive: Status Line");
    RUN_TEST(interactive_status_line_update);
    RUN_TEST(interactive_status_line_thinking_indicator);

    TEST_SUITE("Interactive: Loader");
    RUN_TEST(interactive_loader_displays_thinking);
    RUN_TEST(interactive_loader_tick_updates);

    TEST_SUITE("Interactive: Input");
    RUN_TEST(interactive_input_capture_and_clear);

    TEST_SUITE("Interactive: Session Integration");
    RUN_TEST(interactive_session_create);
    RUN_TEST(interactive_session_append_messages);
    RUN_TEST(interactive_session_persist_and_reload);

    TEST_SUITE("Interactive: Agent State");
    RUN_TEST(interactive_agent_state_add_messages);

    TEST_SUITE("Interactive: Full Flow");
    RUN_TEST(interactive_full_flow_user_to_display);

    TEST_REPORT();
}
