/* test_path_sandbox.c — tests for path sandboxing */
#include "test.h"
#include "harness/path_sandbox.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

/* ========== Create/Free ========== */

TEST(sandbox_create_free) {
    PathSandbox *sb = sandbox_create("/tmp");
    ASSERT_NOT_NULL(sb);
    ASSERT_NOT_NULL(sb->project_root);
    ASSERT_EQ(sb->allowed_count, 0);
    ASSERT_TRUE(sb->allow_home_config);
    sandbox_free(sb);
}

TEST(sandbox_create_null_rejected) {
    PathSandbox *sb = sandbox_create(NULL);
    ASSERT_NULL(sb);
}

TEST(sandbox_create_nonexistent_rejected) {
    PathSandbox *sb = sandbox_create("/nonexistent_path_xyz_12345");
    ASSERT_NULL(sb);
}

/* ========== Project path allowed ========== */

TEST(project_path_allowed) {
    PathSandbox *sb = sandbox_create("/tmp");
    ASSERT_TRUE(sandbox_check(sb, "/tmp"));
    /* Non-existent file in existing parent resolves via parent */
    ASSERT_TRUE(sandbox_check(sb, "/tmp/somefile.txt"));
    /* For deeper non-existent paths, create the intermediate dir */
    mkdir("/tmp/rig_sandbox_subdir", 0755);
    ASSERT_TRUE(sandbox_check(sb, "/tmp/rig_sandbox_subdir/file.txt"));
    rmdir("/tmp/rig_sandbox_subdir");
    sandbox_free(sb);
}

/* ========== Parent blocked ========== */

TEST(parent_path_blocked) {
    /* Create a subdirectory to use as project root */
    mkdir("/tmp/rig_sandbox_test", 0755);
    PathSandbox *sb = sandbox_create("/tmp/rig_sandbox_test");
    ASSERT_NOT_NULL(sb);

    /* Parent of project root should be blocked */
    ASSERT_FALSE(sandbox_check(sb, "/etc/passwd"));
    ASSERT_FALSE(sandbox_check(sb, "/var/log/syslog"));

    sandbox_free(sb);
    rmdir("/tmp/rig_sandbox_test");
}

/* ========== Traversal blocked ========== */

TEST(traversal_blocked) {
    mkdir("/tmp/rig_sandbox_test2", 0755);
    PathSandbox *sb = sandbox_create("/tmp/rig_sandbox_test2");
    ASSERT_NOT_NULL(sb);

    /* Path traversal via .. should resolve outside sandbox */
    ASSERT_FALSE(sandbox_check(sb, "/tmp/rig_sandbox_test2/../../../etc/passwd"));

    sandbox_free(sb);
    rmdir("/tmp/rig_sandbox_test2");
}

/* ========== /tmp allowed after sandbox_allow ========== */

TEST(additional_path_allowed) {
    mkdir("/tmp/rig_sandbox_root", 0755);
    mkdir("/tmp/rig_sandbox_extra", 0755);

    PathSandbox *sb = sandbox_create("/tmp/rig_sandbox_root");
    ASSERT_NOT_NULL(sb);

    /* Extra path not allowed initially */
    ASSERT_FALSE(sandbox_check(sb, "/tmp/rig_sandbox_extra/file.txt"));

    /* Add it */
    ASSERT_EQ(sandbox_allow(sb, "/tmp/rig_sandbox_extra"), 0);

    /* Now it should be allowed */
    ASSERT_TRUE(sandbox_check(sb, "/tmp/rig_sandbox_extra/file.txt"));

    sandbox_free(sb);
    rmdir("/tmp/rig_sandbox_root");
    rmdir("/tmp/rig_sandbox_extra");
}

TEST(allow_duplicate_ok) {
    PathSandbox *sb = sandbox_create("/tmp");
    ASSERT_EQ(sandbox_allow(sb, "/tmp"), 0);
    ASSERT_EQ(sandbox_allow(sb, "/tmp"), 0);  /* duplicate is idempotent */
    sandbox_free(sb);
}

TEST(allow_null_rejected) {
    PathSandbox *sb = sandbox_create("/tmp");
    ASSERT_EQ(sandbox_allow(sb, NULL), -1);
    ASSERT_EQ(sandbox_allow(NULL, "/tmp"), -1);
    sandbox_free(sb);
}

/* ========== Symlink blocked ========== */

TEST(symlink_outside_blocked) {
    mkdir("/tmp/rig_sandbox_sym", 0755);
    PathSandbox *sb = sandbox_create("/tmp/rig_sandbox_sym");
    sb->allow_home_config = false;

    /* Create a symlink inside sandbox pointing outside */
    unlink("/tmp/rig_sandbox_sym/escape");
    symlink("/etc", "/tmp/rig_sandbox_sym/escape");

    /* The resolved path is /etc, which is outside sandbox */
    ASSERT_FALSE(sandbox_check(sb, "/tmp/rig_sandbox_sym/escape/passwd"));

    unlink("/tmp/rig_sandbox_sym/escape");
    sandbox_free(sb);
    rmdir("/tmp/rig_sandbox_sym");
}

/* ========== Resolve ========== */

TEST(resolve_valid_path) {
    PathSandbox *sb = sandbox_create("/tmp");
    char *resolved = sandbox_resolve(sb, "/tmp");
    ASSERT_NOT_NULL(resolved);
    ASSERT_TRUE(strlen(resolved) > 0);
    free(resolved);
    sandbox_free(sb);
}

TEST(resolve_blocked_returns_null) {
    mkdir("/tmp/rig_sandbox_resolve", 0755);
    PathSandbox *sb = sandbox_create("/tmp/rig_sandbox_resolve");
    sb->allow_home_config = false;

    char *resolved = sandbox_resolve(sb, "/etc/passwd");
    ASSERT_NULL(resolved);

    sandbox_free(sb);
    rmdir("/tmp/rig_sandbox_resolve");
}

/* ========== NULL checks ========== */

TEST(check_null_args) {
    PathSandbox *sb = sandbox_create("/tmp");
    ASSERT_FALSE(sandbox_check(sb, NULL));
    ASSERT_FALSE(sandbox_check(NULL, "/tmp"));
    ASSERT_NULL(sandbox_resolve(sb, NULL));
    ASSERT_NULL(sandbox_resolve(NULL, "/tmp"));
    sandbox_free(sb);
}

int main(void) {
    TEST_SUITE("Path Sandbox: Create/Free");
    RUN_TEST(sandbox_create_free);
    RUN_TEST(sandbox_create_null_rejected);
    RUN_TEST(sandbox_create_nonexistent_rejected);

    TEST_SUITE("Path Sandbox: Access Control");
    RUN_TEST(project_path_allowed);
    RUN_TEST(parent_path_blocked);
    RUN_TEST(traversal_blocked);
    RUN_TEST(additional_path_allowed);
    RUN_TEST(allow_duplicate_ok);
    RUN_TEST(allow_null_rejected);

    TEST_SUITE("Path Sandbox: Symlinks");
    RUN_TEST(symlink_outside_blocked);

    TEST_SUITE("Path Sandbox: Resolve");
    RUN_TEST(resolve_valid_path);
    RUN_TEST(resolve_blocked_returns_null);

    TEST_SUITE("Path Sandbox: NULL Checks");
    RUN_TEST(check_null_args);

    TEST_REPORT();
}
