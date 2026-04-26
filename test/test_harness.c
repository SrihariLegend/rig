/* test_harness.c — tests for config, auth, system_prompt, tools */
#include "test.h"
#include "harness/config.h"
#include "harness/auth.h"
#include "harness/system_prompt.h"
#include "harness/tools/tools.h"
#include "ai/types.h"
#include "util/fs.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========== Config ========== */

TEST(config_agent_dir_not_null) {
    const char *d = config_agent_dir();
    ASSERT_NOT_NULL(d);
    ASSERT_TRUE(strlen(d) > 0);
}

TEST(config_settings_global_path) {
    const char *p = config_settings_global_path();
    ASSERT_NOT_NULL(p);
}

TEST(config_settings_project_path) {
    const char *p = config_settings_project_path();
    ASSERT_NOT_NULL(p);
}

TEST(config_auth_path) {
    const char *p = config_auth_path();
    ASSERT_NOT_NULL(p);
}

TEST(config_models_path) {
    const char *p = config_models_path();
    ASSERT_NOT_NULL(p);
}

TEST(config_sessions_dir) {
    const char *p = config_sessions_dir();
    ASSERT_NOT_NULL(p);
}

/* ========== Auth ========== */

TEST(auth_get_api_key_null) {
    char *key = auth_get_api_key(NULL);
    /* May return NULL or an env var; just don't crash */
    free(key);
}

TEST(auth_get_api_key_unknown) {
    char *key = auth_get_api_key("nonexistent_provider_xyz");
    free(key);
}

/* ========== System Prompt ========== */

TEST(system_prompt_build_no_tools) {
    char *sp = system_prompt_build(NULL, 0, "/tmp");
    ASSERT_NOT_NULL(sp);
    ASSERT_TRUE(strlen(sp) > 0);
    free(sp);
}

TEST(system_prompt_build_with_tools) {
    Tool t = { .name = "bash", .description = "Run commands" };
    char *sp = system_prompt_build(&t, 1, "/tmp");
    ASSERT_NOT_NULL(sp);
    ASSERT_TRUE(strstr(sp, "bash") != NULL);
    free(sp);
}

/* ========== Tools ========== */

TEST(tool_bash_create) {
    Tool t = tool_bash_create("/tmp");
    ASSERT_STR_EQ(t.name, "bash");
    ASSERT_NOT_NULL(t.description);
    ASSERT_NOT_NULL(t.parameters);
    ASSERT_NOT_NULL(t.execute);
    cJSON_Delete(t.parameters);
}

TEST(tool_read_create) {
    Tool t = tool_read_create();
    ASSERT_STR_EQ(t.name, "read");
    ASSERT_NOT_NULL(t.execute);
    cJSON_Delete(t.parameters);
}

TEST(tool_write_create) {
    Tool t = tool_write_create();
    ASSERT_STR_EQ(t.name, "write");
    ASSERT_NOT_NULL(t.execute);
    cJSON_Delete(t.parameters);
}

TEST(tool_edit_create) {
    Tool t = tool_edit_create();
    ASSERT_STR_EQ(t.name, "edit");
    ASSERT_NOT_NULL(t.execute);
    cJSON_Delete(t.parameters);
}

TEST(tool_grep_create) {
    Tool t = tool_grep_create();
    ASSERT_STR_EQ(t.name, "grep");
    ASSERT_NOT_NULL(t.execute);
    cJSON_Delete(t.parameters);
}

TEST(tool_ls_create) {
    Tool t = tool_ls_create();
    ASSERT_STR_EQ(t.name, "ls");
    ASSERT_NOT_NULL(t.execute);
    cJSON_Delete(t.parameters);
}

TEST(tool_bash_execute) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo test123");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    ASSERT_NOT_NULL(content);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(tool_read_execute) {
    const char *path = "/tmp/pi_test_read_tool.txt";
    fs_write_file(path, "hello from read", 15);

    Tool t = tool_read_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", path);
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
    unlink(path);
}

TEST(tool_write_execute) {
    Tool t = tool_write_create();
    const char *path = "/tmp/pi_test_write_tool.txt";
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", path);
    cJSON_AddStringToObject(params, "content", "written");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    size_t len;
    char *data = fs_read_file(path, &len);
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(strstr(data, "written") != NULL);
    free(data);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
    unlink(path);
}

TEST(tool_ls_execute) {
    Tool t = tool_ls_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", "/tmp");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(tool_grep_execute) {
    const char *path = "/tmp/pi_test_grep_dir";
    fs_mkdir_p(path);
    fs_write_file("/tmp/pi_test_grep_dir/file.txt", "findme here\nanother line\n", 25);

    Tool t = tool_grep_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "pattern", "findme");
    cJSON_AddStringToObject(params, "path", path);
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
    unlink("/tmp/pi_test_grep_dir/file.txt");
    rmdir(path);
}

/* ========== ADVERSARIAL: Bash tool injection ========== */

TEST(adv_bash_shell_metacharacters) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    /* Command with shell metacharacters -- should not crash, just execute and return */
    cJSON_AddStringToObject(params, "command", "echo safe; echo injected");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_subshell_injection) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo $(whoami)");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    /* Should not crash, just runs the command */
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_backtick_injection) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo `whoami`");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_pipe_injection) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo hi | cat");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_long_command) {
    Tool t = tool_bash_create("/tmp");
    /* Build a 10KB+ command string */
    char *long_cmd = malloc(11000);
    memset(long_cmd, 'A', 10999);
    long_cmd[0] = '#'; /* make it a comment so shell doesn't choke */
    long_cmd[10999] = '\0';
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", long_cmd);
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    /* Should not crash */
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    free(long_cmd);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_null_bytes) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    /* cJSON strings are null-terminated, so embedded nulls get truncated.
       The point is that bash_execute doesn't crash on what cJSON delivers. */
    cJSON_AddStringToObject(params, "command", "echo hello");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_empty_command) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    /* Empty command should be rejected */
    ASSERT_EQ(rc, -1);
    ASSERT_TRUE(count > 0);
    ASSERT_TRUE(strstr(content[0].text.text, "Error") != NULL);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_newlines_in_command) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "echo line1\necho line2\necho line3");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_bash_no_command_param) {
    Tool t = tool_bash_create("/tmp");
    cJSON *params = cJSON_CreateObject();
    /* No "command" key at all */
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

/* ========== ADVERSARIAL: File path traversal (read tool) ========== */

TEST(adv_read_path_traversal) {
    Tool t = tool_read_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "/tmp/../../../etc/hostname");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    /* Should not crash; may or may not read the file depending on permissions */
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_TRUE(rc == 0 || rc == -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_read_null_byte_path) {
    Tool t = tool_read_create();
    cJSON *params = cJSON_CreateObject();
    /* cJSON truncates at null, so this tests with just "/tmp/test" */
    cJSON_AddStringToObject(params, "file_path", "/tmp/test");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    /* Should not crash */
    ASSERT_TRUE(rc == 0 || rc == -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_read_symlink_following) {
    /* Create a symlink that points somewhere and try to read through it */
    unlink("/tmp/pi_test_symlink");
    symlink("/etc/hostname", "/tmp/pi_test_symlink");
    Tool t = tool_read_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "/tmp/pi_test_symlink");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    /* Should not crash - symlinks are followed but this tests for stability */
    ASSERT_TRUE(rc == 0 || rc == -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
    unlink("/tmp/pi_test_symlink");
}

TEST(adv_read_extremely_long_path) {
    Tool t = tool_read_create();
    /* PATH_MAX is typically 4096, create a path exceeding it */
    size_t pathlen = 5000;
    char *long_path = malloc(pathlen + 1);
    memset(long_path, 'a', pathlen);
    long_path[0] = '/';
    long_path[pathlen] = '\0';
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", long_path);
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    /* Should not crash; file won't exist */
    ASSERT_TRUE(rc == 0 || rc == -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    free(long_path);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_read_unicode_path) {
    /* Create a file with unicode chars in path */
    const char *path = "/tmp/pi_test_\xc3\xa9\xc3\xa0\xc3\xbc.txt";
    fs_write_file(path, "unicode test", 12);
    Tool t = tool_read_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", path);
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
    unlink(path);
}

TEST(adv_read_binary_file_rejected) {
    /* Write a file with null bytes (binary) */
    const char *path = "/tmp/pi_test_binary.bin";
    char buf[100];
    memset(buf, 0, 100);
    buf[0] = 'M'; buf[1] = 'Z'; /* PE header-like */
    fs_write_file(path, buf, 100);
    Tool t = tool_read_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", path);
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    /* Binary file should be rejected */
    ASSERT_TRUE(strstr(content[0].text.text, "binary") != NULL);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
    unlink(path);
}

/* ========== ADVERSARIAL: Write tool path traversal ========== */

TEST(adv_write_no_path) {
    Tool t = tool_write_create();
    cJSON *params = cJSON_CreateObject();
    /* Missing file_path */
    cJSON_AddStringToObject(params, "content", "data");
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

TEST(adv_write_no_content) {
    Tool t = tool_write_create();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "file_path", "/tmp/pi_adv_test.txt");
    /* Missing content */
    ContentBlock *content = NULL;
    int count = 0;
    cJSON *details = NULL;
    bool terminate = false;
    int rc = t.execute("call1", params, NULL, NULL, NULL, &content, &count, &details, &terminate);
    ASSERT_EQ(rc, -1);
    ASSERT_TRUE(count > 0);
    for (int i = 0; i < count; i++) content_block_free(&content[i]);
    free(content);
    cJSON_Delete(params);
    cJSON_Delete(details);
    cJSON_Delete(t.parameters);
}

int main(void) {
    TEST_SUITE("Config");
    RUN_TEST(config_agent_dir_not_null);
    RUN_TEST(config_settings_global_path);
    RUN_TEST(config_settings_project_path);
    RUN_TEST(config_auth_path);
    RUN_TEST(config_models_path);
    RUN_TEST(config_sessions_dir);

    TEST_SUITE("Auth");
    RUN_TEST(auth_get_api_key_null);
    RUN_TEST(auth_get_api_key_unknown);

    TEST_SUITE("System Prompt");
    RUN_TEST(system_prompt_build_no_tools);
    RUN_TEST(system_prompt_build_with_tools);

    TEST_SUITE("Tools");
    RUN_TEST(tool_bash_create);
    RUN_TEST(tool_read_create);
    RUN_TEST(tool_write_create);
    RUN_TEST(tool_edit_create);
    RUN_TEST(tool_grep_create);
    RUN_TEST(tool_ls_create);
    RUN_TEST(tool_bash_execute);
    RUN_TEST(tool_read_execute);
    RUN_TEST(tool_write_execute);
    RUN_TEST(tool_ls_execute);
    RUN_TEST(tool_grep_execute);

    TEST_SUITE("ADVERSARIAL: Bash Tool Injection");
    RUN_TEST(adv_bash_shell_metacharacters);
    RUN_TEST(adv_bash_subshell_injection);
    RUN_TEST(adv_bash_backtick_injection);
    RUN_TEST(adv_bash_pipe_injection);
    RUN_TEST(adv_bash_long_command);
    RUN_TEST(adv_bash_null_bytes);
    RUN_TEST(adv_bash_empty_command);
    RUN_TEST(adv_bash_newlines_in_command);
    RUN_TEST(adv_bash_no_command_param);

    TEST_SUITE("ADVERSARIAL: File Path Traversal");
    RUN_TEST(adv_read_path_traversal);
    RUN_TEST(adv_read_null_byte_path);
    RUN_TEST(adv_read_symlink_following);
    RUN_TEST(adv_read_extremely_long_path);
    RUN_TEST(adv_read_unicode_path);
    RUN_TEST(adv_read_binary_file_rejected);
    RUN_TEST(adv_write_no_path);
    RUN_TEST(adv_write_no_content);

    TEST_REPORT();
}
