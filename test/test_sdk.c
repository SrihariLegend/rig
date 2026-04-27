/* test_sdk.c — tests for rig.h SDK */
#include "test.h"
#include "rig.h"
#include <stdlib.h>
#include <string.h>

TEST(rig_version_basic) {
    const char *v = rig_version();
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(v, "0.1.0");
}

TEST(rig_version_macros) {
    ASSERT_EQ(RIG_VERSION_MAJOR, 0);
    ASSERT_EQ(RIG_VERSION_MINOR, 1);
    ASSERT_EQ(RIG_VERSION_PATCH, 0);
}

TEST(rig_create_basic) {
    RigInstance *r = rig_create();
    ASSERT_NOT_NULL(r);
    ASSERT_NOT_NULL(r->api);
    ASSERT_FALSE(r->initialized);
    rig_free(r);
}

TEST(rig_free_null) {
    rig_free(NULL);
}

TEST(rig_create_has_hooks) {
    RigInstance *r = rig_create();
    ASSERT_NOT_NULL(r->api->hooks);
    rig_free(r);
}

TEST(rig_create_has_bus) {
    RigInstance *r = rig_create();
    ASSERT_NOT_NULL(r->api->bus);
    rig_free(r);
}

TEST(rig_create_has_settings) {
    RigInstance *r = rig_create();
    ASSERT_NOT_NULL(r->api->settings);
    rig_free(r);
}

TEST(rig_create_has_state) {
    RigInstance *r = rig_create();
    ASSERT_NOT_NULL(r->api->state);
    rig_free(r);
}

TEST(rig_api_abi_version) {
    RigInstance *r = rig_create();
    ASSERT_EQ(r->api->abi_version, RIG_ABI_VERSION);
    rig_free(r);
}

TEST(rig_run_print_null) {
    ASSERT_EQ(rig_run_print(NULL, "hello"), -1);
}

TEST(rig_run_rpc_null) {
    ASSERT_EQ(rig_run_rpc(NULL), -1);
}

TEST(rig_extension_api_access) {
    RigInstance *r = rig_create();
    Tool *t = calloc(1, sizeof(Tool));
    t->name = strdup("sdk_tool");
    extension_api_register_tool(r->api, t);
    ASSERT_NOT_NULL(extension_api_get_tool(r->api, "sdk_tool"));
    rig_free(r);
}

int main(void) {
    TEST_SUITE("SDK");
    RUN_TEST(rig_version_basic);
    RUN_TEST(rig_version_macros);
    RUN_TEST(rig_create_basic);
    RUN_TEST(rig_free_null);
    RUN_TEST(rig_create_has_hooks);
    RUN_TEST(rig_create_has_bus);
    RUN_TEST(rig_create_has_settings);
    RUN_TEST(rig_create_has_state);
    RUN_TEST(rig_api_abi_version);
    RUN_TEST(rig_run_print_null);
    RUN_TEST(rig_run_rpc_null);
    RUN_TEST(rig_extension_api_access);

    TEST_REPORT();
}
