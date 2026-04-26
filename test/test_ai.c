/* test_ai.c — tests for src/ai/ modules */
#include "test.h"
#include "ai/types.h"
#include "ai/registry.h"
#include "ai/validation.h"
#include "ai/json_parse.h"
#include "ai/transform.h"
#include "ai/models.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>

/* ========== Content Blocks ========== */

TEST(content_text_basic) {
    ContentBlock b = content_text("hello", NULL);
    ASSERT_EQ(b.type, CONTENT_TEXT);
    ASSERT_STR_EQ(b.text.text, "hello");
    ASSERT_NULL(b.text.signature);
    content_block_free(&b);
}

TEST(content_text_with_sig) {
    ContentBlock b = content_text("hello", "sig123");
    ASSERT_STR_EQ(b.text.signature, "sig123");
    content_block_free(&b);
}

TEST(content_thinking_basic) {
    ContentBlock b = content_thinking("thought", "sig", false);
    ASSERT_EQ(b.type, CONTENT_THINKING);
    ASSERT_STR_EQ(b.thinking.thinking, "thought");
    ASSERT_FALSE(b.thinking.redacted);
    content_block_free(&b);
}

TEST(content_thinking_redacted) {
    ContentBlock b = content_thinking("x", NULL, true);
    ASSERT_TRUE(b.thinking.redacted);
    content_block_free(&b);
}

TEST(content_image_basic) {
    ContentBlock b = content_image("base64data", "image/png");
    ASSERT_EQ(b.type, CONTENT_IMAGE);
    ASSERT_STR_EQ(b.image.data, "base64data");
    ASSERT_STR_EQ(b.image.mime_type, "image/png");
    content_block_free(&b);
}

TEST(content_tool_call_basic) {
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "path", "/tmp");
    ContentBlock b = content_tool_call("id1", "read", args);
    ASSERT_EQ(b.type, CONTENT_TOOL_CALL);
    ASSERT_STR_EQ(b.tool_call.id, "id1");
    ASSERT_STR_EQ(b.tool_call.name, "read");
    ASSERT_NOT_NULL(b.tool_call.arguments);
    cJSON_Delete(args);
    content_block_free(&b);
}

TEST(content_tool_call_null_args) {
    ContentBlock b = content_tool_call("id1", "bash", NULL);
    ASSERT_NOT_NULL(b.tool_call.arguments);
    content_block_free(&b);
}

TEST(content_block_clone_text) {
    ContentBlock b = content_text("hello", "sig");
    ContentBlock c = content_block_clone(&b);
    ASSERT_STR_EQ(c.text.text, "hello");
    ASSERT_STR_EQ(c.text.signature, "sig");
    ASSERT_TRUE(c.text.text != b.text.text);
    content_block_free(&b);
    content_block_free(&c);
}

TEST(content_block_clone_tool_call) {
    cJSON *args = cJSON_Parse("{\"x\":1}");
    ContentBlock b = content_tool_call("id", "name", args);
    ContentBlock c = content_block_clone(&b);
    ASSERT_STR_EQ(c.tool_call.id, "id");
    ASSERT_STR_EQ(c.tool_call.name, "name");
    cJSON_Delete(args);
    content_block_free(&b);
    content_block_free(&c);
}

/* ========== Messages ========== */

TEST(message_create_user_basic) {
    Message *m = message_create_user("hello");
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(m->role, ROLE_USER);
    ASSERT_EQ(m->content_count, 1);
    ASSERT_STR_EQ(m->content[0].text.text, "hello");
    ASSERT_TRUE(m->timestamp > 0);
    message_free(m);
}

TEST(message_create_user_null_text) {
    Message *m = message_create_user(NULL);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(m->content_count, 0);
    message_free(m);
}

TEST(message_create_assistant) {
    Message *m = message_create_assistant();
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(m->role, ROLE_ASSISTANT);
    ASSERT_EQ(m->content_count, 0);
    message_free(m);
}

TEST(message_create_tool_result_basic) {
    ContentBlock b = content_text("result", NULL);
    Message *m = message_create_tool_result("tc1", "bash", &b, 1, NULL, false);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(m->role, ROLE_TOOL_RESULT);
    ASSERT_STR_EQ(m->tool_call_id, "tc1");
    ASSERT_STR_EQ(m->tool_name, "bash");
    ASSERT_FALSE(m->is_error);
    ASSERT_EQ(m->content_count, 1);
    content_block_free(&b);
    message_free(m);
}

TEST(message_create_tool_result_error) {
    ContentBlock b = content_text("error", NULL);
    Message *m = message_create_tool_result("tc1", "bash", &b, 1, NULL, true);
    ASSERT_TRUE(m->is_error);
    content_block_free(&b);
    message_free(m);
}

TEST(message_add_content) {
    Message *m = message_create_assistant();
    message_add_content(m, content_text("part1", NULL));
    message_add_content(m, content_text("part2", NULL));
    ASSERT_EQ(m->content_count, 2);
    ASSERT_STR_EQ(m->content[0].text.text, "part1");
    ASSERT_STR_EQ(m->content[1].text.text, "part2");
    message_free(m);
}

TEST(message_clone_basic) {
    Message *m = message_create_user("hello");
    m->api = strdup("anthropic");
    m->provider = strdup("anthropic");
    Message *c = message_clone(m);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->role, ROLE_USER);
    ASSERT_EQ(c->content_count, 1);
    ASSERT_STR_EQ(c->content[0].text.text, "hello");
    ASSERT_STR_EQ(c->api, "anthropic");
    ASSERT_TRUE(c->content[0].text.text != m->content[0].text.text);
    message_free(m);
    message_free(c);
}

TEST(message_clone_null) {
    ASSERT_NULL(message_clone(NULL));
}

TEST(message_free_null) {
    message_free(NULL);
}

/* ========== Model helpers ========== */

TEST(model_supports_images_true) {
    static const char *mods[] = {"text", "image"};
    Model m = { .input_modalities = mods, .input_modality_count = 2 };
    ASSERT_TRUE(model_supports_images(&m));
}

TEST(model_supports_images_false) {
    static const char *mods[] = {"text"};
    Model m = { .input_modalities = mods, .input_modality_count = 1 };
    ASSERT_FALSE(model_supports_images(&m));
}

TEST(model_supports_images_null) {
    ASSERT_FALSE(model_supports_images(NULL));
}

TEST(model_calculate_cost) {
    Model m = { .cost_per_million = { .input = 3.0, .output = 15.0, .cache_read = 0.3, .cache_write = 3.75 } };
    Usage u = { .input_tokens = 1000000, .output_tokens = 100000 };
    double cost = model_calculate_cost(&m, &u);
    ASSERT_FLOAT_EQ(cost, 3.0 + 1.5, 0.01);
}

TEST(model_calculate_cost_null) {
    ASSERT_FLOAT_EQ(model_calculate_cost(NULL, NULL), 0.0, 0.001);
}

TEST(model_supports_xhigh_opus) {
    Model m = { .id = "claude-opus-4-6-20250501" };
    ASSERT_TRUE(model_supports_xhigh(&m));
}

TEST(model_supports_xhigh_sonnet) {
    Model m = { .id = "claude-sonnet-4-6-20250514" };
    ASSERT_FALSE(model_supports_xhigh(&m));
}

/* ========== Registry ========== */

static int mock_stream(const Model *model, const Message *msgs, int count,
    const char *sys, const Tool *tools, int tc,
    const StreamOptions *opts, StreamCallback cb, void *ud) {
    (void)model;(void)msgs;(void)count;(void)sys;(void)tools;(void)tc;(void)opts;(void)cb;(void)ud;
    return 0;
}

TEST(registry_init_cleanup) {
    ai_registry_init();
    ASSERT_NULL(ai_get_provider("test"));
    ai_registry_cleanup();
}

TEST(registry_register_get) {
    ai_registry_init();
    ApiProvider p = { .api = "test-api", .stream = mock_stream };
    ai_register_provider(&p);
    const ApiProvider *got = ai_get_provider("test-api");
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got->api, "test-api");
    ai_registry_cleanup();
}

TEST(registry_get_missing) {
    ai_registry_init();
    ASSERT_NULL(ai_get_provider("nonexistent"));
    ai_registry_cleanup();
}

TEST(registry_unregister) {
    ai_registry_init();
    ApiProvider p = { .api = "test-api", .stream = mock_stream };
    ai_register_provider(&p);
    ai_unregister_provider("test-api");
    ASSERT_NULL(ai_get_provider("test-api"));
    ai_registry_cleanup();
}

TEST(registry_overwrite) {
    ai_registry_init();
    ApiProvider p1 = { .api = "test", .stream = mock_stream };
    ApiProvider p2 = { .api = "test", .stream = NULL };
    ai_register_provider(&p1);
    ai_register_provider(&p2);
    const ApiProvider *got = ai_get_provider("test");
    ASSERT_NULL(got->stream);
    ai_registry_cleanup();
}

/* ========== Validation ========== */

TEST(validation_ok_no_schema) {
    Tool t = { .name = "test", .parameters = NULL };
    cJSON *args = cJSON_CreateObject();
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_TRUE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
}

TEST(validation_required_field_missing) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_CreateObject();
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_FALSE(r.valid);
    ASSERT_NOT_NULL(r.error);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(validation_required_field_present) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_Parse("{\"name\":\"test\"}");
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_TRUE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(validation_wrong_type) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"n\":{\"type\":\"number\"}}}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_Parse("{\"n\":\"not a number\"}");
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_FALSE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(validation_enum_valid) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"color\":{\"type\":\"string\",\"enum\":[\"red\",\"green\"]}}}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_Parse("{\"color\":\"red\"}");
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_TRUE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(validation_enum_invalid) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"color\":{\"type\":\"string\",\"enum\":[\"red\",\"green\"]}}}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_Parse("{\"color\":\"blue\"}");
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_FALSE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(validation_boolean_type) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"b\":{\"type\":\"boolean\"}}}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_Parse("{\"b\":true}");
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_TRUE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(validation_array_type) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"arr\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}}}");
    Tool t = { .name = "test", .parameters = schema };
    cJSON *args = cJSON_Parse("{\"arr\":[\"a\",\"b\"]}");
    ValidationResult r = validate_tool_arguments(&t, args);
    ASSERT_TRUE(r.valid);
    validation_result_free(&r);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(coerce_string_to_number) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"n\":{\"type\":\"number\"}}}");
    cJSON *args = cJSON_Parse("{\"n\":\"42\"}");
    coerce_tool_arguments(schema, args);
    cJSON *n = cJSON_GetObjectItem(args, "n");
    ASSERT_TRUE(cJSON_IsNumber(n));
    ASSERT_EQ(n->valueint, 42);
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(coerce_number_to_boolean) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"b\":{\"type\":\"boolean\"}}}");
    cJSON *args = cJSON_Parse("{\"b\":1}");
    coerce_tool_arguments(schema, args);
    cJSON *b = cJSON_GetObjectItem(args, "b");
    ASSERT_TRUE(cJSON_IsBool(b));
    ASSERT_TRUE(cJSON_IsTrue(b));
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

TEST(coerce_null_to_string) {
    cJSON *schema = cJSON_Parse("{\"type\":\"object\",\"properties\":{\"s\":{\"type\":\"string\"}}}");
    cJSON *args = cJSON_Parse("{\"s\":null}");
    coerce_tool_arguments(schema, args);
    cJSON *s = cJSON_GetObjectItem(args, "s");
    ASSERT_TRUE(cJSON_IsString(s));
    ASSERT_STR_EQ(s->valuestring, "");
    cJSON_Delete(args);
    cJSON_Delete(schema);
}

/* ========== JSON Parse ========== */

TEST(json_parse_repair_valid) {
    cJSON *r = json_parse_repair("{\"a\":1}");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(cJSON_GetObjectItem(r, "a")->valueint, 1);
    cJSON_Delete(r);
}

TEST(json_parse_repair_trailing_comma) {
    cJSON *r = json_parse_repair("{\"a\":1,}");
    ASSERT_NOT_NULL(r);
    cJSON_Delete(r);
}

TEST(json_parse_repair_unclosed) {
    cJSON *r = json_parse_repair("{\"a\":1");
    ASSERT_NOT_NULL(r);
    cJSON_Delete(r);
}

TEST(json_parse_repair_null) {
    ASSERT_NULL(json_parse_repair(NULL));
}

TEST(json_parse_streaming_valid) {
    cJSON *r = json_parse_streaming("{\"x\":2}");
    ASSERT_NOT_NULL(r);
    cJSON_Delete(r);
}

TEST(json_parse_streaming_partial) {
    cJSON *r = json_parse_streaming("{\"x\":2");
    ASSERT_NOT_NULL(r);
    cJSON_Delete(r);
}

/* ========== Transform ========== */

TEST(tool_id_map_create_free) {
    ToolCallIdMap *m = tool_id_map_create();
    ASSERT_NOT_NULL(m);
    tool_id_map_free(m);
}

TEST(tool_id_normalize_no_change) {
    ToolCallIdMap *m = tool_id_map_create();
    char *n = tool_id_normalize(m, "abc123", 64);
    ASSERT_STR_EQ(n, "abc123");
    free(n);
    tool_id_map_free(m);
}

TEST(tool_id_normalize_too_long) {
    ToolCallIdMap *m = tool_id_map_create();
    char *n = tool_id_normalize(m, "abcdef", 3);
    ASSERT_TRUE(strncmp(n, "tc_", 3) == 0);
    ASSERT_EQ(m->count, 1);
    free(n);
    tool_id_map_free(m);
}

TEST(tool_id_normalize_special_chars) {
    ToolCallIdMap *m = tool_id_map_create();
    char *n = tool_id_normalize(m, "foo.bar!baz", 64);
    ASSERT_TRUE(strncmp(n, "tc_", 3) == 0);
    free(n);
    tool_id_map_free(m);
}

TEST(tool_id_lookup_original) {
    ToolCallIdMap *m = tool_id_map_create();
    char *n = tool_id_normalize(m, "original-id!", 5);
    const char *orig = tool_id_lookup_original(m, n);
    ASSERT_STR_EQ(orig, "original-id!");
    free(n);
    tool_id_map_free(m);
}

TEST(tool_id_lookup_not_found) {
    ToolCallIdMap *m = tool_id_map_create();
    const char *orig = tool_id_lookup_original(m, "unknown");
    ASSERT_STR_EQ(orig, "unknown");
    tool_id_map_free(m);
}

/* ========== Models ========== */

TEST(models_get_anthropic) {
    models_init();
    const Model *m = models_get("anthropic", "claude-opus-4-6-20250501");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->name, "Claude Opus 4.6");
}

TEST(models_get_partial_match) {
    models_init();
    const Model *m = models_get("anthropic", "opus-4-6");
    ASSERT_NOT_NULL(m);
}

TEST(models_get_missing) {
    models_init();
    ASSERT_NULL(models_get("anthropic", "nonexistent-model"));
}

TEST(models_get_null_id) {
    models_init();
    ASSERT_NULL(models_get(NULL, NULL));
}

TEST(models_get_all) {
    models_init();
    int count = 0;
    const Model **all = models_get_all("anthropic", &count);
    ASSERT_TRUE(count >= 3);
    ASSERT_NOT_NULL(all);
}

TEST(models_get_all_no_filter) {
    models_init();
    int count = 0;
    const Model **all = models_get_all(NULL, &count);
    ASSERT_TRUE(count >= 4);
    ASSERT_NOT_NULL(all);
}

TEST(models_get_providers) {
    models_init();
    int count = 0;
    const char **provs = models_get_providers(&count);
    ASSERT_TRUE(count >= 2);
    ASSERT_NOT_NULL(provs);
}

/* ========== ADVERSARIAL: Malformed JSON parsing ========== */

TEST(adv_json_truncated) {
    cJSON *r = json_parse_repair("{\"key\": \"val");
    /* Repair may or may not succeed, but must not crash */
    if (r) cJSON_Delete(r);
}

TEST(adv_json_deeply_nested) {
    /* Build 100+ levels of nesting: [[[[...]]]]] */
    char buf[300];
    int pos = 0;
    for (int i = 0; i < 100 && pos < 200; i++) buf[pos++] = '[';
    buf[pos++] = '1';
    for (int i = 0; i < 100 && pos < 298; i++) buf[pos++] = ']';
    buf[pos] = '\0';
    cJSON *r = json_parse_repair(buf);
    /* Must not crash; cJSON has a depth limit so may return NULL */
    if (r) cJSON_Delete(r);
}

TEST(adv_json_huge_string_value) {
    /* Build JSON with a 1MB string value */
    size_t val_len = 1024 * 1024;
    size_t total = val_len + 32;
    char *buf = malloc(total);
    ASSERT_NOT_NULL(buf);
    int prefix = snprintf(buf, total, "{\"big\":\"");
    memset(buf + prefix, 'A', val_len);
    snprintf(buf + prefix + val_len, total - prefix - val_len, "\"}");
    cJSON *r = json_parse_repair(buf);
    /* Must not crash */
    if (r) cJSON_Delete(r);
    free(buf);
}

TEST(adv_json_invalid_utf8) {
    /* JSON with invalid UTF-8 byte sequences */
    const char *bad = "{\"key\": \"\xff\xfe\xfd\"}";
    cJSON *r = json_parse_repair(bad);
    /* cJSON may accept or reject, but must not crash */
    if (r) cJSON_Delete(r);
}

TEST(adv_json_null_bytes_in_string) {
    /* cJSON stops at null terminator so this is effectively short input */
    cJSON *r = json_parse_repair("{\"a\":");
    if (r) cJSON_Delete(r);
}

TEST(adv_json_empty_string) {
    cJSON *r = json_parse_repair("");
    ASSERT_NULL(r);
}

TEST(adv_json_just_whitespace) {
    cJSON *r = json_parse_repair("   \t\n\r  ");
    ASSERT_NULL(r);
}

TEST(adv_json_array_100k_elements) {
    /* Build JSON array with 100000 small elements: [0,1,2,...] */
    size_t buf_size = 100000 * 8 + 16;
    char *buf = malloc(buf_size);
    ASSERT_NOT_NULL(buf);
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < 100000; i++) {
        if (i > 0) buf[pos++] = ',';
        pos += snprintf(buf + pos, buf_size - pos, "%d", i % 1000);
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    cJSON *r = json_parse_repair(buf);
    /* Must not crash */
    if (r) {
        ASSERT_TRUE(cJSON_IsArray(r));
        cJSON_Delete(r);
    }
    free(buf);
}

TEST(adv_json_duplicate_keys) {
    cJSON *r = json_parse_repair("{\"a\":1,\"a\":2}");
    ASSERT_NOT_NULL(r);
    /* cJSON keeps both or last; must not crash */
    cJSON_Delete(r);
}

TEST(adv_json_float_limits) {
    cJSON *r = json_parse_repair("{\"big\":1.7976931348623157e+308,\"small\":-1.7976931348623157e+308,\"tiny\":5e-324}");
    ASSERT_NOT_NULL(r);
    cJSON_Delete(r);
}

TEST(adv_json_streaming_null) {
    ASSERT_NULL(json_parse_streaming(NULL));
}

int main(void) {
    TEST_SUITE("Content Blocks");
    RUN_TEST(content_text_basic);
    RUN_TEST(content_text_with_sig);
    RUN_TEST(content_thinking_basic);
    RUN_TEST(content_thinking_redacted);
    RUN_TEST(content_image_basic);
    RUN_TEST(content_tool_call_basic);
    RUN_TEST(content_tool_call_null_args);
    RUN_TEST(content_block_clone_text);
    RUN_TEST(content_block_clone_tool_call);

    TEST_SUITE("Messages");
    RUN_TEST(message_create_user_basic);
    RUN_TEST(message_create_user_null_text);
    RUN_TEST(message_create_assistant);
    RUN_TEST(message_create_tool_result_basic);
    RUN_TEST(message_create_tool_result_error);
    RUN_TEST(message_add_content);
    RUN_TEST(message_clone_basic);
    RUN_TEST(message_clone_null);
    RUN_TEST(message_free_null);

    TEST_SUITE("Model Helpers");
    RUN_TEST(model_supports_images_true);
    RUN_TEST(model_supports_images_false);
    RUN_TEST(model_supports_images_null);
    RUN_TEST(model_calculate_cost);
    RUN_TEST(model_calculate_cost_null);
    RUN_TEST(model_supports_xhigh_opus);
    RUN_TEST(model_supports_xhigh_sonnet);

    TEST_SUITE("Registry");
    RUN_TEST(registry_init_cleanup);
    RUN_TEST(registry_register_get);
    RUN_TEST(registry_get_missing);
    RUN_TEST(registry_unregister);
    RUN_TEST(registry_overwrite);

    TEST_SUITE("Validation");
    RUN_TEST(validation_ok_no_schema);
    RUN_TEST(validation_required_field_missing);
    RUN_TEST(validation_required_field_present);
    RUN_TEST(validation_wrong_type);
    RUN_TEST(validation_enum_valid);
    RUN_TEST(validation_enum_invalid);
    RUN_TEST(validation_boolean_type);
    RUN_TEST(validation_array_type);
    RUN_TEST(coerce_string_to_number);
    RUN_TEST(coerce_number_to_boolean);
    RUN_TEST(coerce_null_to_string);

    TEST_SUITE("JSON Parse/Repair");
    RUN_TEST(json_parse_repair_valid);
    RUN_TEST(json_parse_repair_trailing_comma);
    RUN_TEST(json_parse_repair_unclosed);
    RUN_TEST(json_parse_repair_null);
    RUN_TEST(json_parse_streaming_valid);
    RUN_TEST(json_parse_streaming_partial);

    TEST_SUITE("ADVERSARIAL: Malformed JSON");
    RUN_TEST(adv_json_truncated);
    RUN_TEST(adv_json_deeply_nested);
    RUN_TEST(adv_json_huge_string_value);
    RUN_TEST(adv_json_invalid_utf8);
    RUN_TEST(adv_json_null_bytes_in_string);
    RUN_TEST(adv_json_empty_string);
    RUN_TEST(adv_json_just_whitespace);
    RUN_TEST(adv_json_array_100k_elements);
    RUN_TEST(adv_json_duplicate_keys);
    RUN_TEST(adv_json_float_limits);
    RUN_TEST(adv_json_streaming_null);

    TEST_SUITE("Transform");
    RUN_TEST(tool_id_map_create_free);
    RUN_TEST(tool_id_normalize_no_change);
    RUN_TEST(tool_id_normalize_too_long);
    RUN_TEST(tool_id_normalize_special_chars);
    RUN_TEST(tool_id_lookup_original);
    RUN_TEST(tool_id_lookup_not_found);

    TEST_SUITE("Models");
    RUN_TEST(models_get_anthropic);
    RUN_TEST(models_get_partial_match);
    RUN_TEST(models_get_missing);
    RUN_TEST(models_get_null_id);
    RUN_TEST(models_get_all);
    RUN_TEST(models_get_all_no_filter);
    RUN_TEST(models_get_providers);

    TEST_REPORT();
}
