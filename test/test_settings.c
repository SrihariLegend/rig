/* test_settings.c — tests for settings manager */
#include "test.h"
#include "harness/settings.h"
#include "util/fs.h"
#include "util/json.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void cleanup(void) {
    unlink("/tmp/pi_test_global_settings.json");
    unlink("/tmp/pi_test_project_settings.json");
}

TEST(settings_create_basic) {
    cleanup();
    SettingsManager *sm = settings_create(NULL, NULL);
    ASSERT_NOT_NULL(sm);
    ASSERT_NOT_NULL(sm->merged);
    settings_free(sm);
}

TEST(settings_defaults_present) {
    SettingsManager *sm = settings_create(NULL, NULL);
    /* Built-in defaults */
    const char *theme = settings_get_string(sm, "theme", NULL);
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme, "default");
    ASSERT_TRUE(settings_get_bool(sm, "compaction.enabled", false));
    settings_free(sm);
}

TEST(settings_three_layer_merge) {
    const char *gp = "/tmp/pi_test_global_settings.json";
    const char *pp = "/tmp/pi_test_project_settings.json";
    cleanup();
    /* Write global: theme=dark */
    cJSON *g = cJSON_CreateObject();
    cJSON_AddStringToObject(g, "theme", "dark");
    json_write_file(gp, g);
    cJSON_Delete(g);

    /* Write project: theme=light */
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "theme", "light");
    json_write_file(pp, p);
    cJSON_Delete(p);

    SettingsManager *sm = settings_create(gp, pp);
    /* Project overrides global */
    ASSERT_STR_EQ(settings_get_string(sm, "theme", ""), "light");

    /* CLI overrides project */
    settings_set(sm, SETTINGS_CLI, "theme", cJSON_CreateString("custom"));
    ASSERT_STR_EQ(settings_get_string(sm, "theme", ""), "custom");

    settings_free(sm);
    cleanup();
}

TEST(settings_dot_notation) {
    SettingsManager *sm = settings_create(NULL, NULL);
    settings_set(sm, SETTINGS_CLI, "ui.font.size", cJSON_CreateNumber(14));
    ASSERT_EQ(settings_get_int(sm, "ui.font.size", 0), 14);
    settings_free(sm);
}

TEST(settings_typed_getters) {
    SettingsManager *sm = settings_create(NULL, NULL);
    settings_set(sm, SETTINGS_CLI, "str_key", cJSON_CreateString("hello"));
    settings_set(sm, SETTINGS_CLI, "int_key", cJSON_CreateNumber(42));
    settings_set(sm, SETTINGS_CLI, "bool_key", cJSON_CreateTrue());
    settings_set(sm, SETTINGS_CLI, "double_key", cJSON_CreateNumber(3.14));

    ASSERT_STR_EQ(settings_get_string(sm, "str_key", ""), "hello");
    ASSERT_EQ(settings_get_int(sm, "int_key", 0), 42);
    ASSERT_TRUE(settings_get_bool(sm, "bool_key", false));
    ASSERT_FLOAT_EQ(settings_get_double(sm, "double_key", 0), 3.14, 0.01);

    /* Defaults when missing */
    ASSERT_STR_EQ(settings_get_string(sm, "missing", "def"), "def");
    ASSERT_EQ(settings_get_int(sm, "missing", 99), 99);
    ASSERT_FALSE(settings_get_bool(sm, "missing", false));
    settings_free(sm);
}

TEST(settings_flush_reload) {
    const char *gp = "/tmp/pi_test_global_settings.json";
    cleanup();
    SettingsManager *sm = settings_create(gp, NULL);
    settings_set(sm, SETTINGS_GLOBAL, "saved_key", cJSON_CreateString("saved_val"));
    ASSERT_EQ(settings_flush(sm), 0);
    ASSERT_TRUE(fs_exists(gp));

    /* Reload and verify */
    ASSERT_EQ(settings_reload(sm), 0);
    ASSERT_STR_EQ(settings_get_string(sm, "saved_key", ""), "saved_val");
    settings_free(sm);
    cleanup();
}

TEST(settings_null_deletion) {
    SettingsManager *sm = settings_create(NULL, NULL);
    /* Set in global, delete in CLI */
    settings_set(sm, SETTINGS_GLOBAL, "to_delete", cJSON_CreateString("val"));
    ASSERT_STR_EQ(settings_get_string(sm, "to_delete", ""), "val");

    /* Setting NULL creates a cJSON_NULL which prune_nulls removes */
    settings_set(sm, SETTINGS_CLI, "to_delete", NULL);
    cJSON *item = settings_get(sm, "to_delete");
    ASSERT_NULL(item);
    settings_free(sm);
}

TEST(settings_free_null) {
    settings_free(NULL);
}

TEST(settings_get_null) {
    ASSERT_NULL(settings_get(NULL, "key"));
}

TEST(settings_set_null_sm) {
    ASSERT_EQ(settings_set(NULL, SETTINGS_CLI, "key", cJSON_CreateString("v")), -1);
}

int main(void) {
    TEST_SUITE("Settings");
    RUN_TEST(settings_create_basic);
    RUN_TEST(settings_defaults_present);
    RUN_TEST(settings_three_layer_merge);
    RUN_TEST(settings_dot_notation);
    RUN_TEST(settings_typed_getters);
    RUN_TEST(settings_flush_reload);
    RUN_TEST(settings_null_deletion);
    RUN_TEST(settings_free_null);
    RUN_TEST(settings_get_null);
    RUN_TEST(settings_set_null_sm);

    TEST_REPORT();
}
