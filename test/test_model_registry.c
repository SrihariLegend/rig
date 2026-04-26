/* test_model_registry.c — tests for src/harness/model_registry.c */
#include "test.h"
#include "harness/model_registry.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ========== Lifecycle ========== */

TEST(registry_create_free) {
    ModelRegistry *mr = model_registry_create();
    ASSERT_NOT_NULL(mr);
    ASSERT_TRUE(mr->builtin_count > 0);
    model_registry_free(mr);
}

TEST(registry_free_null) {
    model_registry_free(NULL); /* must not crash */
}

/* ========== Exact match ========== */

TEST(resolve_exact_id) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "claude-opus-4-6-20250501");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->id, "claude-opus-4-6-20250501");
    model_registry_free(mr);
}

TEST(resolve_exact_gpt) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "gpt-4o-2024-11-20");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->id, "gpt-4o-2024-11-20");
    model_registry_free(mr);
}

/* ========== Substring match ========== */

TEST(resolve_substring_sonnet) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "sonnet");
    ASSERT_NOT_NULL(m);
    ASSERT_TRUE(strstr(m->id, "sonnet") != NULL);
    model_registry_free(mr);
}

TEST(resolve_substring_opus) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "opus");
    ASSERT_NOT_NULL(m);
    ASSERT_TRUE(strstr(m->id, "opus") != NULL);
    model_registry_free(mr);
}

TEST(resolve_substring_haiku) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "haiku");
    ASSERT_NOT_NULL(m);
    ASSERT_TRUE(strstr(m->id, "haiku") != NULL);
    model_registry_free(mr);
}

TEST(resolve_substring_gpt4o) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "gpt-4o");
    ASSERT_NOT_NULL(m);
    ASSERT_TRUE(strstr(m->id, "gpt-4o") != NULL);
    model_registry_free(mr);
}

/* ========== Name match ========== */

TEST(resolve_name_match) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "Haiku 4.5");
    ASSERT_NOT_NULL(m);
    ASSERT_TRUE(strstr(m->name, "Haiku") != NULL);
    model_registry_free(mr);
}

/* ========== No match ========== */

TEST(resolve_no_match) {
    ModelRegistry *mr = model_registry_create();
    Model *m = model_registry_resolve(mr, "nonexistent-model-xyz");
    ASSERT_NULL(m);
    model_registry_free(mr);
}

TEST(resolve_null_pattern) {
    ModelRegistry *mr = model_registry_create();
    ASSERT_NULL(model_registry_resolve(mr, NULL));
    ASSERT_NULL(model_registry_resolve(mr, ""));
    model_registry_free(mr);
}

TEST(resolve_null_registry) {
    ASSERT_NULL(model_registry_resolve(NULL, "opus"));
}

/* ========== List all ========== */

TEST(list_all) {
    ModelRegistry *mr = model_registry_create();
    int count = 0;
    Model **list = model_registry_list(mr, &count);
    ASSERT_NOT_NULL(list);
    ASSERT_TRUE(count >= 4); /* at least the 5 builtins */
    free(list);
    model_registry_free(mr);
}

/* ========== List by provider ========== */

TEST(list_provider_anthropic) {
    ModelRegistry *mr = model_registry_create();
    int count = 0;
    Model **list = model_registry_list_provider(mr, "anthropic", &count);
    ASSERT_NOT_NULL(list);
    ASSERT_TRUE(count >= 3);
    for (int i = 0; i < count; i++) {
        ASSERT_STR_EQ(list[i]->provider, "anthropic");
    }
    free(list);
    model_registry_free(mr);
}

TEST(list_provider_openai) {
    ModelRegistry *mr = model_registry_create();
    int count = 0;
    Model **list = model_registry_list_provider(mr, "openai", &count);
    ASSERT_NOT_NULL(list);
    ASSERT_TRUE(count >= 1);
    free(list);
    model_registry_free(mr);
}

TEST(list_provider_none) {
    ModelRegistry *mr = model_registry_create();
    int count = 0;
    Model **list = model_registry_list_provider(mr, "nonexistent", &count);
    ASSERT_NULL(list);
    ASSERT_EQ(count, 0);
    model_registry_free(mr);
}

/* ========== Custom model loading ========== */

static const char *CUSTOM_MODELS_JSON =
    "[\n"
    "  {\n"
    "    \"id\": \"my-custom-model-v1\",\n"
    "    \"name\": \"My Custom Model\",\n"
    "    \"api\": \"openai-completions\",\n"
    "    \"provider\": \"custom-provider\",\n"
    "    \"base_url\": \"https://custom.example.com/v1\",\n"
    "    \"context_window\": 32000,\n"
    "    \"max_tokens\": 8192,\n"
    "    \"input_modalities\": [\"text\"]\n"
    "  },\n"
    "  {\n"
    "    \"id\": \"my-custom-model-v2\",\n"
    "    \"name\": \"My Custom Model V2\",\n"
    "    \"api\": \"anthropic-messages\",\n"
    "    \"provider\": \"custom-provider\",\n"
    "    \"base_url\": \"https://custom.example.com/v2\",\n"
    "    \"context_window\": 64000,\n"
    "    \"max_tokens\": 16384\n"
    "  }\n"
    "]\n";

TEST(load_custom_models) {
    /* Write temp JSON file */
    char path[] = "/tmp/test_models_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0);
    write(fd, CUSTOM_MODELS_JSON, strlen(CUSTOM_MODELS_JSON));
    close(fd);

    ModelRegistry *mr = model_registry_create();
    int ret = model_registry_load_custom(mr, path);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(mr->custom_count, 2);

    /* Custom model should be resolvable */
    Model *m = model_registry_resolve(mr, "my-custom-model-v1");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->name, "My Custom Model");
    ASSERT_EQ(m->context_window, 32000);

    /* Custom model appears in full list */
    int count = 0;
    Model **list = model_registry_list(mr, &count);
    ASSERT_TRUE(count >= 6); /* 5 builtin + 2 custom */
    free(list);

    /* Custom model appears in provider list */
    count = 0;
    list = model_registry_list_provider(mr, "custom-provider", &count);
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(count, 2);
    free(list);

    model_registry_free(mr);
    unlink(path);
}

TEST(load_custom_missing_file) {
    ModelRegistry *mr = model_registry_create();
    int ret = model_registry_load_custom(mr, "/tmp/nonexistent_models_12345.json");
    ASSERT_EQ(ret, -1);
    model_registry_free(mr);
}

TEST(load_custom_invalid_json) {
    char path[] = "/tmp/test_models_bad_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0);
    const char *bad = "not valid json{{{";
    write(fd, bad, strlen(bad));
    close(fd);

    ModelRegistry *mr = model_registry_create();
    int ret = model_registry_load_custom(mr, path);
    ASSERT_EQ(ret, -1);
    model_registry_free(mr);
    unlink(path);
}

TEST(load_custom_null_args) {
    ModelRegistry *mr = model_registry_create();
    ASSERT_EQ(model_registry_load_custom(mr, NULL), -1);
    ASSERT_EQ(model_registry_load_custom(NULL, "/tmp/foo"), -1);
    model_registry_free(mr);
}

/* ========== Custom model priority ========== */

TEST(custom_model_exact_match_priority) {
    char path[] = "/tmp/test_models_pri_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0);
    /* Custom model with same-ish name to test priority */
    const char *json = "[{\"id\":\"my-opus\",\"name\":\"My Opus\",\"api\":\"custom\",\"provider\":\"custom\"}]";
    write(fd, json, strlen(json));
    close(fd);

    ModelRegistry *mr = model_registry_create();
    model_registry_load_custom(mr, path);

    /* Exact match on custom ID */
    Model *m = model_registry_resolve(mr, "my-opus");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->id, "my-opus");

    model_registry_free(mr);
    unlink(path);
}

int main(void) {
    TEST_SUITE("Model Registry: Lifecycle");
    RUN_TEST(registry_create_free);
    RUN_TEST(registry_free_null);

    TEST_SUITE("Model Registry: Exact Match");
    RUN_TEST(resolve_exact_id);
    RUN_TEST(resolve_exact_gpt);

    TEST_SUITE("Model Registry: Substring Match");
    RUN_TEST(resolve_substring_sonnet);
    RUN_TEST(resolve_substring_opus);
    RUN_TEST(resolve_substring_haiku);
    RUN_TEST(resolve_substring_gpt4o);

    TEST_SUITE("Model Registry: Name Match");
    RUN_TEST(resolve_name_match);

    TEST_SUITE("Model Registry: No Match");
    RUN_TEST(resolve_no_match);
    RUN_TEST(resolve_null_pattern);
    RUN_TEST(resolve_null_registry);

    TEST_SUITE("Model Registry: List");
    RUN_TEST(list_all);
    RUN_TEST(list_provider_anthropic);
    RUN_TEST(list_provider_openai);
    RUN_TEST(list_provider_none);

    TEST_SUITE("Model Registry: Custom Models");
    RUN_TEST(load_custom_models);
    RUN_TEST(load_custom_missing_file);
    RUN_TEST(load_custom_invalid_json);
    RUN_TEST(load_custom_null_args);
    RUN_TEST(custom_model_exact_match_priority);

    TEST_REPORT();
}
