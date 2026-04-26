/* test_rpc.c — tests for RPC server */
#include "test.h"
#include "harness/modes/rpc.h"
#include "harness/extensions/extension.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

TEST(rpc_parse_request_basic) {
    RPCRequest *req = rpc_parse_request("{\"method\":\"ping\",\"id\":\"1\",\"params\":{}}");
    ASSERT_NOT_NULL(req);
    ASSERT_STR_EQ(req->method, "ping");
    ASSERT_STR_EQ(req->id, "1");
    rpc_request_free(req);
}

TEST(rpc_parse_request_numeric_id) {
    RPCRequest *req = rpc_parse_request("{\"method\":\"test\",\"id\":42}");
    ASSERT_NOT_NULL(req);
    ASSERT_STR_EQ(req->id, "42");
    rpc_request_free(req);
}

TEST(rpc_parse_request_no_id) {
    RPCRequest *req = rpc_parse_request("{\"method\":\"notify\"}");
    ASSERT_NOT_NULL(req);
    ASSERT_NULL(req->id);
    rpc_request_free(req);
}

TEST(rpc_parse_request_null) {
    ASSERT_NULL(rpc_parse_request(NULL));
}

TEST(rpc_parse_request_invalid) {
    ASSERT_NULL(rpc_parse_request("not json"));
}

TEST(rpc_format_response_result) {
    cJSON *result = cJSON_CreateString("pong");
    RPCResponse resp = { .id = "1", .result = result };
    char *json = rpc_format_response(&resp);
    ASSERT_NOT_NULL(json);
    cJSON *parsed = cJSON_Parse(json);
    ASSERT_STR_EQ(cJSON_GetObjectItem(parsed, "jsonrpc")->valuestring, "2.0");
    ASSERT_STR_EQ(cJSON_GetObjectItem(parsed, "id")->valuestring, "1");
    ASSERT_STR_EQ(cJSON_GetObjectItem(parsed, "result")->valuestring, "pong");
    cJSON_Delete(parsed);
    free(json);
    cJSON_Delete(result);
}

TEST(rpc_format_response_error) {
    RPCResponse resp = { .id = "1", .error = "not found", .error_code = -32601 };
    char *json = rpc_format_response(&resp);
    ASSERT_NOT_NULL(json);
    cJSON *parsed = cJSON_Parse(json);
    cJSON *err = cJSON_GetObjectItem(parsed, "error");
    ASSERT_NOT_NULL(err);
    ASSERT_EQ(cJSON_GetObjectItem(err, "code")->valueint, -32601);
    cJSON_Delete(parsed);
    free(json);
}

TEST(rpc_format_notification) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "msg", "hello");
    char *json = rpc_format_notification("update", params);
    ASSERT_NOT_NULL(json);
    cJSON *parsed = cJSON_Parse(json);
    ASSERT_STR_EQ(cJSON_GetObjectItem(parsed, "method")->valuestring, "update");
    cJSON_Delete(parsed);
    cJSON_Delete(params);
    free(json);
}

TEST(rpc_format_notification_null) {
    ASSERT_NULL(rpc_format_notification(NULL, NULL));
}

TEST(rpc_server_create_free) {
    PiExtensionAPI *api = extension_api_create();
    RPCServer *s = rpc_server_create(api);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->api, api);
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_handle_ping) {
    PiExtensionAPI *api = extension_api_create();
    RPCServer *s = rpc_server_create(api);
    /* redirect output to /dev/null */
    int old_fd = s->output_fd;
    s->output_fd = open("/dev/null", 1);
    rpc_handle_message(s, "{\"method\":\"ping\",\"id\":\"1\"}");
    close(s->output_fd);
    s->output_fd = old_fd;
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_handle_version) {
    PiExtensionAPI *api = extension_api_create();
    RPCServer *s = rpc_server_create(api);
    s->output_fd = open("/dev/null", 1);
    rpc_handle_message(s, "{\"method\":\"version\",\"id\":\"2\"}");
    close(s->output_fd);
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_handle_shutdown) {
    PiExtensionAPI *api = extension_api_create();
    RPCServer *s = rpc_server_create(api);
    s->output_fd = open("/dev/null", 1);
    s->running = true;
    rpc_handle_message(s, "{\"method\":\"shutdown\",\"id\":\"3\"}");
    ASSERT_FALSE(s->running);
    close(s->output_fd);
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_handle_list_tools) {
    PiExtensionAPI *api = extension_api_create();
    Tool *t = calloc(1, sizeof(Tool));
    t->name = strdup("test_tool");
    extension_api_register_tool(api, t);

    RPCServer *s = rpc_server_create(api);
    s->output_fd = open("/dev/null", 1);
    rpc_handle_message(s, "{\"method\":\"list_tools\",\"id\":\"4\"}");
    close(s->output_fd);
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_handle_unknown_method) {
    PiExtensionAPI *api = extension_api_create();
    RPCServer *s = rpc_server_create(api);
    s->output_fd = open("/dev/null", 1);
    rpc_handle_message(s, "{\"method\":\"nonexistent\",\"id\":\"5\"}");
    close(s->output_fd);
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_handle_invalid_json) {
    PiExtensionAPI *api = extension_api_create();
    RPCServer *s = rpc_server_create(api);
    s->output_fd = open("/dev/null", 1);
    int rc = rpc_handle_message(s, "not json");
    ASSERT_EQ(rc, -1);
    close(s->output_fd);
    rpc_server_free(s);
    extension_api_free(api);
}

TEST(rpc_request_free_null) {
    rpc_request_free(NULL);
}

int main(void) {
    TEST_SUITE("RPC");
    RUN_TEST(rpc_parse_request_basic);
    RUN_TEST(rpc_parse_request_numeric_id);
    RUN_TEST(rpc_parse_request_no_id);
    RUN_TEST(rpc_parse_request_null);
    RUN_TEST(rpc_parse_request_invalid);
    RUN_TEST(rpc_format_response_result);
    RUN_TEST(rpc_format_response_error);
    RUN_TEST(rpc_format_notification);
    RUN_TEST(rpc_format_notification_null);
    RUN_TEST(rpc_server_create_free);
    RUN_TEST(rpc_handle_ping);
    RUN_TEST(rpc_handle_version);
    RUN_TEST(rpc_handle_shutdown);
    RUN_TEST(rpc_handle_list_tools);
    RUN_TEST(rpc_handle_unknown_method);
    RUN_TEST(rpc_handle_invalid_json);
    RUN_TEST(rpc_request_free_null);

    TEST_REPORT();
}
