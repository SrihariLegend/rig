#ifndef RIG_MODE_RPC_H
#define RIG_MODE_RPC_H

#include "ai/types.h"
#include "harness/extensions/extension.h"

typedef struct {
    int input_fd;
    int output_fd;
    RigExtensionAPI *api;
    bool running;
} RPCServer;

RPCServer *rpc_server_create(RigExtensionAPI *api);
void rpc_server_free(RPCServer *server);

int rpc_server_start(RPCServer *server);
void rpc_server_stop(RPCServer *server);

int rpc_handle_message(RPCServer *server, const char *json_line);

typedef struct {
    char *method;
    char *id;
    cJSON *params;
} RPCRequest;

typedef struct {
    char *id;
    cJSON *result;
    char *error;
    int error_code;
} RPCResponse;

RPCRequest *rpc_parse_request(const char *json);
void rpc_request_free(RPCRequest *req);

char *rpc_format_response(RPCResponse *resp);
char *rpc_format_notification(const char *method, cJSON *params);
void rpc_response_free(RPCResponse *resp);

int rpc_send_response(RPCServer *server, RPCResponse *resp);
int rpc_send_notification(RPCServer *server, const char *method, cJSON *params);

#endif
