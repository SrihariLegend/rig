/* test_themes.c — tests for src/harness/themes.c */
#include "test.h"
#include "harness/themes.h"
#include "util/fs.h"
#include "util/json.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

TEST(theme_load_default_basic) {
    Theme *t = theme_load_default();
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Dark");
    ASSERT_TRUE(t->var_count > 0);
    ASSERT_TRUE(t->color_count > 0);
    theme_free(t);
}

TEST(theme_load_default_vars) {
    Theme *t = theme_load_default();
    /* Check primary var exists */
    bool found = false;
    for (int i = 0; i < t->var_count; i++) {
        if (strcmp(t->vars[i].key, "primary") == 0) {
            found = true;
            ASSERT_STR_EQ(t->vars[i].value, "39");
        }
    }
    ASSERT_TRUE(found);
    theme_free(t);
}

TEST(theme_resolve_color_accent) {
    Theme *t = theme_load_default();
    char *c = theme_resolve_color(t, "accent");
    ASSERT_NOT_NULL(c);
    /* accent -> primary -> 39 -> ANSI 256-color */
    ASSERT_TRUE(strstr(c, "\033[38;5;39m") != NULL);
    free(c);
    theme_free(t);
}

TEST(theme_resolve_color_unknown) {
    Theme *t = theme_load_default();
    char *c = theme_resolve_color(t, "nonexistent_token");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, "");
    free(c);
    theme_free(t);
}

TEST(theme_resolve_color_null) {
    char *c = theme_resolve_color(NULL, "accent");
    ASSERT_STR_EQ(c, "");
    free(c);
}

TEST(theme_load_from_file) {
    const char *path = "/tmp/pi_test_theme.json";
    const char *json = "{\"name\":\"Test\",\"vars\":{\"primary\":\"#ff0000\"},\"colors\":{\"accent\":\"primary\"}}";
    fs_write_file(path, json, strlen(json));

    Theme *t = theme_load(path);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Test");
    ASSERT_EQ(t->var_count, 1);
    ASSERT_STR_EQ(t->vars[0].key, "primary");

    char *c = theme_resolve_color(t, "accent");
    ASSERT_TRUE(strstr(c, "\033[38;2;255;0;0m") != NULL);
    free(c);

    theme_free(t);
    unlink(path);
}

TEST(theme_load_null) {
    ASSERT_NULL(theme_load(NULL));
}

TEST(theme_load_missing) {
    ASSERT_NULL(theme_load("/tmp/pi_nonexistent_theme.json"));
}

TEST(theme_free_null) {
    theme_free(NULL);
}

TEST(themes_discover_basic) {
    const char *dir = "/tmp/pi_test_themes_dir";
    system("rm -rf /tmp/pi_test_themes_dir");
    fs_mkdir_p(dir);
    fs_write_file("/tmp/pi_test_themes_dir/dark.json", "{}", 2);
    fs_write_file("/tmp/pi_test_themes_dir/light.json", "{}", 2);

    int count = 0;
    const char *paths[] = { dir };
    char **found = themes_discover(paths, 1, &count);
    ASSERT_EQ(count, 2);
    ASSERT_NOT_NULL(found);
    for (int i = 0; i < count; i++) free(found[i]);
    free(found);
    system("rm -rf /tmp/pi_test_themes_dir");
}

TEST(themes_discover_empty) {
    int count = 0;
    const char *paths[] = { "/tmp/pi_nonexistent_themes" };
    char **found = themes_discover(paths, 1, &count);
    ASSERT_EQ(count, 0);
    /* found may be NULL */
    if (found) free(found);
}

int main(void) {
    TEST_SUITE("Themes");
    RUN_TEST(theme_load_default_basic);
    RUN_TEST(theme_load_default_vars);
    RUN_TEST(theme_resolve_color_accent);
    RUN_TEST(theme_resolve_color_unknown);
    RUN_TEST(theme_resolve_color_null);
    RUN_TEST(theme_load_from_file);
    RUN_TEST(theme_load_null);
    RUN_TEST(theme_load_missing);
    RUN_TEST(theme_free_null);
    RUN_TEST(themes_discover_basic);
    RUN_TEST(themes_discover_empty);

    TEST_REPORT();
}
