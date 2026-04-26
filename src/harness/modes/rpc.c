#include "rpc.h"
#include "util/str.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

RPCServer *rpc_server_create(PiExtensionAPI *api) {
    RPCServer *server = calloc(1, sizeof(RPCServer));
    if (!server) return NULL;

    server->api = api;
    server->input_fd = STDIN_FILENO;
    server->output_fd = STDOUT_FILENO;

    return server;
}

void rpc_server_free(RPCServer *server) {
    if (!server) return;
    free(server);
}

RPCRequest *rpc_parse_request(const char *json) {
    if (!json) return NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root) return NULL;

    RPCRequest *req = calloc(1, sizeof(RPCRequest));
    if (!req) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *method = cJSON_GetObjectItem(root, "method");
    if (method && cJSON_IsString(method)) {
        req->method = strdup(method->valuestring);
    }

    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id && cJSON_IsString(id)) {
        req->id = strdup(id->valuestring);
    } else if (id && cJSON_IsNumber(id)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", id->valueint);
        req->id = strdup(buf);
    }

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params) {
        req->params = cJSON_Duplicate(params, true);
    }

    cJSON_Delete(root);
    return req;
}

void rpc_request_free(RPCRequest *req) {
    if (!req) return;
    free(req->method);
    free(req->id);
    if (req->params) cJSON_Delete(req->params);
    free(req);
}

char *rpc_format_response(RPCResponse *resp) {
    if (!resp) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");

    if (resp->id) {
        cJSON_AddStringToObject(root, "id", resp->id);
    }

    if (resp->error) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code", resp->error_code);
        cJSON_AddStringToObject(err, "message", resp->error);
        cJSON_AddItemToObject(root, "error", err);
    } else if (resp->result) {
        cJSON_AddItemToObject(root, "result", cJSON_Duplicate(resp->result, true));
    } else {
        cJSON_AddNullToObject(root, "result");
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *rpc_format_notification(const char *method, cJSON *params) {
    if (!method) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);

    if (params) {
        cJSON_AddItemToObject(root, "params", cJSON_Duplicate(params, true));
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void rpc_response_free(RPCResponse *resp) {
    if (!resp) return;
    free(resp->id);
    if (resp->result) cJSON_Delete(resp->result);
    free(resp->error);
    free(resp);
}

static int write_jsonl(int fd, const char *json) {
    if (!json) return -1;
    size_t len = strlen(json);

    Str line = str_from(json);
    str_append_char(&line, '\n');

    ssize_t written = write(fd, line.data, line.len);
    str_free(&line);

    return (written == (ssize_t)(len + 1)) ? 0 : -1;
}

int rpc_send_response(RPCServer *server, RPCResponse *resp) {
    if (!server || !resp) return -1;

    char *json = rpc_format_response(resp);
    if (!json) return -1;

    int result = write_jsonl(server->output_fd, json);
    free(json);
    return result;
}

int rpc_send_notification(RPCServer *server, const char *method, cJSON *params) {
    if (!server || !method) return -1;

    char *json = rpc_format_notification(method, params);
    if (!json) return -1;

    int result = write_jsonl(server->output_fd, json);
    free(json);
    return result;
}

int rpc_handle_message(RPCServer *server, const char *json_line) {
    if (!server || !json_line) return -1;

    RPCRequest *req = rpc_parse_request(json_line);
    if (!req) {
        RPCResponse resp = {
            .error = "Parse error",
            .error_code = -32700,
        };
        rpc_send_response(server, &resp);
        return -1;
    }

    RPCResponse *resp = calloc(1, sizeof(RPCResponse));
    resp->id = req->id ? strdup(req->id) : NULL;

    if (!req->method) {
        resp->error = strdup("Method not found");
        resp->error_code = -32601;
    } else if (strcmp(req->method, "ping") == 0) {
        resp->result = cJSON_CreateString("pong");
    } else if (strcmp(req->method, "version") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "version", "0.1.0");
        cJSON_AddNumberToObject(result, "abi", PI_ABI_VERSION);
        resp->result = result;
    } else if (strcmp(req->method, "list_tools") == 0) {
        cJSON *tools = cJSON_CreateArray();
        if (server->api) {
            for (int i = 0; i < server->api->tool_count; i++) {
                if (server->api->tools[i]) {
                    cJSON_AddItemToArray(tools,
                        cJSON_CreateString(server->api->tools[i]->name));
                }
            }
        }
        resp->result = tools;
    } else if (strcmp(req->method, "list_commands") == 0) {
        cJSON *cmds = cJSON_CreateArray();
        if (server->api) {
            for (int i = 0; i < server->api->command_count; i++) {
                cJSON_AddItemToArray(cmds,
                    cJSON_CreateString(server->api->commands[i].name));
            }
        }
        resp->result = cmds;
    } else if (strcmp(req->method, "shutdown") == 0) {
        server->running = false;
        resp->result = cJSON_CreateString("ok");
    } else {
        resp->error = strdup("Method not found");
        resp->error_code = -32601;
    }

    rpc_send_response(server, resp);

    rpc_request_free(req);
    rpc_response_free(resp);
    return 0;
}

int rpc_server_start(RPCServer *server) {
    if (!server) return -1;

    server->running = true;
    char buf[65536];
    Str line = str_new(1024);

    while (server->running) {
        ssize_t n = read(server->input_fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';

        str_append(&line, buf);

        char *newline;
        while ((newline = strchr(line.data, '\n')) != NULL) {
            *newline = '\0';
            if (line.data[0] != '\0') {
                rpc_handle_message(server, line.data);
            }

            size_t remaining = line.len - (newline - line.data + 1);
            memmove(line.data, newline + 1, remaining);
            line.len = remaining;
            line.data[line.len] = '\0';
        }
    }

    str_free(&line);
    return 0;
}

void rpc_server_stop(RPCServer *server) {
    if (server) server->running = false;
}
