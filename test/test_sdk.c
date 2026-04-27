/* test_sdk.c — tests for pi.h SDK */
#include "test.h"
#include "pi.h"
#include <stdlib.h>
#include <string.h>

TEST(pi_version_basic) {
    const char *v = pi_version();
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(v, "0.1.0");
}

TEST(pi_version_macros) {
    ASSERT_EQ(RIG_VERSION_MAJOR, 0);
    ASSERT_EQ(RIG_VERSION_MINOR, 1);
    ASSERT_EQ(RIG_VERSION_PATCH, 0);
}

TEST(pi_create_basic) {
    PiInstance *pi = pi_create();
    ASSERT_NOT_NULL(pi);
    ASSERT_NOT_NULL(pi->api);
    ASSERT_FALSE(pi->initialized);
    pi_free(pi);
}

TEST(pi_free_null) {
    pi_free(NULL);
}

TEST(pi_create_has_hooks) {
    PiInstance *pi = pi_create();
    ASSERT_NOT_NULL(pi->api->hooks);
    pi_free(pi);
}

TEST(pi_create_has_bus) {
    PiInstance *pi = pi_create();
    ASSERT_NOT_NULL(pi->api->bus);
    pi_free(pi);
}

TEST(pi_create_has_settings) {
    PiInstance *pi = pi_create();
    ASSERT_NOT_NULL(pi->api->settings);
    pi_free(pi);
}

TEST(pi_create_has_state) {
    PiInstance *pi = pi_create();
    ASSERT_NOT_NULL(pi->api->state);
    pi_free(pi);
}

TEST(pi_api_abi_version) {
    PiInstance *pi = pi_create();
    ASSERT_EQ(pi->api->abi_version, PI_ABI_VERSION);
    pi_free(pi);
}

TEST(pi_run_print_null) {
    ASSERT_EQ(pi_run_print(NULL, "hello"), -1);
}

TEST(pi_run_rpc_null) {
    ASSERT_EQ(pi_run_rpc(NULL), -1);
}

TEST(pi_extension_api_access) {
    PiInstance *pi = pi_create();
    Tool *t = calloc(1, sizeof(Tool));
    t->name = strdup("sdk_tool");
    extension_api_register_tool(pi->api, t);
    ASSERT_NOT_NULL(extension_api_get_tool(pi->api, "sdk_tool"));
    pi_free(pi);
}

int main(void) {
    TEST_SUITE("SDK");
    RUN_TEST(pi_version_basic);
    RUN_TEST(pi_version_macros);
    RUN_TEST(pi_create_basic);
    RUN_TEST(pi_free_null);
    RUN_TEST(pi_create_has_hooks);
    RUN_TEST(pi_create_has_bus);
    RUN_TEST(pi_create_has_settings);
    RUN_TEST(pi_create_has_state);
    RUN_TEST(pi_api_abi_version);
    RUN_TEST(pi_run_print_null);
    RUN_TEST(pi_run_rpc_null);
    RUN_TEST(pi_extension_api_access);

    TEST_REPORT();
}
