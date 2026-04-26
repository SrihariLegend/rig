/* test_export.c — tests for src/harness/export.c */
#include "test.h"
#include "harness/export.h"
#include "harness/session.h"
#include "util/fs.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TEST_DIR = "/tmp/pi_test_export";

static void cleanup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

/* Helper: create a session with a user message */
static Session *make_session_with_user_msg(const char *text) {
    Session *s = session_create(TEST_DIR);
    if (!s) return NULL;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "user");
    cJSON *arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", text);
    cJSON_AddItemToArray(arr, block);
    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);

    return s;
}

/* Helper: add an assistant message with text */
static void add_assistant_msg(Session *s, const char *text) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "assistant");
    cJSON *arr = cJSON_AddArrayToObject(data, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", text);
    cJSON_AddItemToArray(arr, block);
    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);
}

/* Helper: add assistant message with thinking block */
static void add_assistant_with_thinking(Session *s, const char *text, const char *thinking) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "assistant");
    cJSON *arr = cJSON_AddArrayToObject(data, "content");

    cJSON *tb = cJSON_CreateObject();
    cJSON_AddStringToObject(tb, "type", "thinking");
    cJSON_AddStringToObject(tb, "thinking", thinking);
    cJSON_AddItemToArray(arr, tb);

    cJSON *txt = cJSON_CreateObject();
    cJSON_AddStringToObject(txt, "type", "text");
    cJSON_AddStringToObject(txt, "text", text);
    cJSON_AddItemToArray(arr, txt);

    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);
}

/* Helper: add assistant message with tool call */
static void add_assistant_with_tool(Session *s, const char *tool_name) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "role", "assistant");
    cJSON *arr = cJSON_AddArrayToObject(data, "content");

    cJSON *tc = cJSON_CreateObject();
    cJSON_AddStringToObject(tc, "type", "tool_call");
    cJSON_AddStringToObject(tc, "id", "call_123");
    cJSON_AddStringToObject(tc, "name", tool_name);
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "path", "/tmp/test");
    cJSON_AddItemToObject(tc, "arguments", args);
    cJSON_AddItemToArray(arr, tc);

    session_append(s, ENTRY_MESSAGE, data);
    cJSON_Delete(data);
}

TEST(export_empty_session) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);

    char *html = session_export_html_string(s, NULL);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "<!DOCTYPE html>") != NULL);
    ASSERT_TRUE(strstr(html, "</html>") != NULL);
    ASSERT_TRUE(strstr(html, "charset=\"UTF-8\"") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_user_assistant_messages) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("Hello world");
    add_assistant_msg(s, "Hi there!");

    char *html = session_export_html_string(s, NULL);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "Hello world") != NULL);
    ASSERT_TRUE(strstr(html, "Hi there!") != NULL);
    ASSERT_TRUE(strstr(html, "msg-user") != NULL);
    ASSERT_TRUE(strstr(html, "msg-assistant") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_thinking_included) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("question");
    add_assistant_with_thinking(s, "answer", "deep thought");

    ExportConfig cfg = export_config_defaults();
    cfg.include_thinking = true;
    char *html = session_export_html_string(s, &cfg);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "deep thought") != NULL);
    ASSERT_TRUE(strstr(html, "Thinking") != NULL);
    ASSERT_TRUE(strstr(html, "<details>") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_thinking_excluded) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("question");
    add_assistant_with_thinking(s, "answer", "deep thought");

    ExportConfig cfg = export_config_defaults();
    cfg.include_thinking = false;
    char *html = session_export_html_string(s, &cfg);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "deep thought") == NULL);
    ASSERT_TRUE(strstr(html, "answer") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_tool_calls) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("do something");
    add_assistant_with_tool(s, "bash_tool");

    ExportConfig cfg = export_config_defaults();
    cfg.include_tool_calls = true;
    char *html = session_export_html_string(s, &cfg);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "bash_tool") != NULL);
    ASSERT_TRUE(strstr(html, "Tool:") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_dark_theme) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("test");

    ExportConfig cfg = export_config_defaults();
    cfg.dark_theme = true;
    char *html = session_export_html_string(s, &cfg);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "#1e1e2e") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_light_theme) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("test");

    ExportConfig cfg = export_config_defaults();
    cfg.dark_theme = false;
    char *html = session_export_html_string(s, &cfg);
    ASSERT_NOT_NULL(html);
    /* Light theme uses #ffffff background */
    ASSERT_TRUE(strstr(html, "#ffffff") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_html_escaping) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("<script>alert('xss')</script>");

    char *html = session_export_html_string(s, NULL);
    ASSERT_NOT_NULL(html);
    /* Must NOT contain raw <script> */
    ASSERT_TRUE(strstr(html, "<script>alert") == NULL);
    /* Must contain escaped version */
    ASSERT_TRUE(strstr(html, "&lt;script&gt;") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_to_file) {
    cleanup_test_dir();
    Session *s = make_session_with_user_msg("file test");

    const char *out_path = "/tmp/pi_test_export/output.html";
    fs_mkdir_p(TEST_DIR);
    ExportConfig cfg = export_config_defaults();
    int rc = session_export_html(s, out_path, &cfg);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(fs_exists(out_path));

    size_t len = 0;
    char *content = fs_read_file(out_path, &len);
    ASSERT_NOT_NULL(content);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(content, "file test") != NULL);

    free(content);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_utf8_preserved) {
    cleanup_test_dir();
    /* Chinese + emoji */
    Session *s = make_session_with_user_msg("\xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80 test");

    char *html = session_export_html_string(s, NULL);
    ASSERT_NOT_NULL(html);
    ASSERT_TRUE(strstr(html, "\xE4\xB8\xAD\xE6\x96\x87") != NULL);
    ASSERT_TRUE(strstr(html, "\xF0\x9F\x98\x80") != NULL);

    free(html);
    session_free(s);
    cleanup_test_dir();
}

TEST(export_config_defaults_work) {
    ExportConfig cfg = export_config_defaults();
    ASSERT_TRUE(cfg.include_thinking);
    ASSERT_TRUE(cfg.include_tool_calls);
    ASSERT_TRUE(cfg.include_timestamps);
    ASSERT_TRUE(cfg.dark_theme);
    ASSERT_NULL(cfg.title);
}

TEST(export_null_session) {
    char *html = session_export_html_string(NULL, NULL);
    ASSERT_NULL(html);
    ASSERT_EQ(session_export_html(NULL, "/tmp/out.html", NULL), -1);
}

TEST(export_null_path) {
    cleanup_test_dir();
    Session *s = session_create(TEST_DIR);
    ASSERT_EQ(session_export_html(s, NULL, NULL), -1);
    session_free(s);
    cleanup_test_dir();
}

int main(void) {
    TEST_SUITE("HTML Export");
    RUN_TEST(export_empty_session);
    RUN_TEST(export_user_assistant_messages);
    RUN_TEST(export_thinking_included);
    RUN_TEST(export_thinking_excluded);
    RUN_TEST(export_tool_calls);
    RUN_TEST(export_dark_theme);
    RUN_TEST(export_light_theme);
    RUN_TEST(export_html_escaping);
    RUN_TEST(export_to_file);
    RUN_TEST(export_utf8_preserved);
    RUN_TEST(export_config_defaults_work);
    RUN_TEST(export_null_session);
    RUN_TEST(export_null_path);
    TEST_REPORT();
}
