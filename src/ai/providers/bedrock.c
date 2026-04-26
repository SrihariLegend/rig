#include "bedrock.h"
#include "sigv4.h"
#include "aws_eventstream.h"
#include "ai/types.h"
#include "util/str.h"
#include "util/http.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    StreamCallback cb;
    void *userdata;
    Message *partial;
} BedrockStreamCtx;

static void bedrock_sse_handler(const char *event_type, const char *data, void *userdata) {
    BedrockStreamCtx *ctx = (BedrockStreamCtx *)userdata;
    (void)event_type;

    if (!data) return;

    cJSON *json = cJSON_Parse(data);
    if (!json) return;

    cJSON *type = cJSON_GetObjectItem(json, "type");
    if (!type || !cJSON_IsString(type)) {
        cJSON_Delete(json);
        return;
    }

    if (strcmp(type->valuestring, "message_start") == 0) {
        if (!ctx->partial) {
            ctx->partial = calloc(1, sizeof(Message));
            ctx->partial->role = ROLE_ASSISTANT;
        }
        StreamEvent evt = { .type = EVENT_START, .partial = ctx->partial };
        ctx->cb(&evt, ctx->userdata);
    } else if (strcmp(type->valuestring, "content_block_start") == 0) {
        if (!ctx->partial) {
            ctx->partial = calloc(1, sizeof(Message));
            ctx->partial->role = ROLE_ASSISTANT;
        }
        StreamEvent evt = { .type = EVENT_TEXT_START, .partial = ctx->partial };
        ctx->cb(&evt, ctx->userdata);
    } else if (strcmp(type->valuestring, "content_block_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(json, "delta");
        if (delta) {
            cJSON *dt = cJSON_GetObjectItem(delta, "type");
            if (dt && cJSON_IsString(dt) && strcmp(dt->valuestring, "text_delta") == 0) {
                cJSON *text = cJSON_GetObjectItem(delta, "text");
                if (text && cJSON_IsString(text)) {
                    StreamEvent evt = { .type = EVENT_TEXT_DELTA, .delta = text->valuestring };
                    ctx->cb(&evt, ctx->userdata);
                }
            }
        }
    } else if (strcmp(type->valuestring, "content_block_stop") == 0) {
        StreamEvent evt = { .type = EVENT_TEXT_END, .partial = ctx->partial };
        ctx->cb(&evt, ctx->userdata);
    } else if (strcmp(type->valuestring, "message_delta") == 0) {
        /* usage info etc — ignore for now */
    } else if (strcmp(type->valuestring, "message_stop") == 0) {
        if (!ctx->partial) {
            ctx->partial = calloc(1, sizeof(Message));
            ctx->partial->role = ROLE_ASSISTANT;
        }
        StreamEvent done = { .type = EVENT_DONE, .message = ctx->partial };
        ctx->cb(&done, ctx->userdata);
        ctx->partial = NULL;
    }

    cJSON_Delete(json);
}

static cJSON *build_body(const Model *model, const Message *messages, int msg_count,
                         const char *system_prompt) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "anthropic_version", "bedrock-2023-05-31");
    cJSON_AddNumberToObject(body, "max_tokens", model->max_tokens > 0 ? model->max_tokens : 4096);

    if (system_prompt) {
        cJSON_AddStringToObject(body, "system", system_prompt);
    }

    cJSON *msgs = cJSON_CreateArray();
    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role",
            messages[i].role == ROLE_USER ? "user" : "assistant");
        cJSON *content = cJSON_CreateArray();
        for (int j = 0; j < messages[i].content_count; j++) {
            if (messages[i].content[j].type == CONTENT_TEXT) {
                cJSON *block = cJSON_CreateObject();
                cJSON_AddStringToObject(block, "type", "text");
                cJSON_AddStringToObject(block, "text",
                    messages[i].content[j].text.text ? messages[i].content[j].text.text : "");
                cJSON_AddItemToArray(content, block);
            }
        }
        cJSON_AddItemToObject(msg, "content", content);
        cJSON_AddItemToArray(msgs, msg);
    }
    cJSON_AddItemToObject(body, "messages", msgs);

    return body;
}

static const char *get_bedrock_api_key(void) {
    const char *key = getenv("AWS_BEARER_TOKEN_BEDROCK");
    if (key && key[0]) return key;
    key = getenv("BEDROCK_API_KEY");
    if (key && key[0]) return key;
    return NULL;
}

typedef struct {
    BedrockStreamCtx *stream_ctx;
    EventStreamParser *parser;
} BedrockRawCtx;

static void bedrock_event_handler(const char *json_event, void *ctx) {
    BedrockStreamCtx *stream_ctx = (BedrockStreamCtx *)ctx;
    bedrock_sse_handler(NULL, json_event, stream_ctx);
}

static void bedrock_raw_data(const unsigned char *data, size_t len, void *ctx) {
    BedrockRawCtx *raw = (BedrockRawCtx *)ctx;
    eventstream_parser_feed(raw->parser, data, len);
}

static int bedrock_stream_apikey(const Model *model, const Message *messages, int msg_count,
                                 const char *system_prompt, StreamCallback cb, void *userdata) {
    const char *api_key = get_bedrock_api_key();
    const char *region = getenv("AWS_REGION");
    if (!region) region = "us-east-1";

    char url[512];
    snprintf(url, sizeof(url),
        "https://bedrock-runtime.%s.amazonaws.com/model/%s/invoke-with-response-stream",
        region, model->id);

    cJSON *body = build_body(model, messages, msg_count, system_prompt);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    const char *headers[] = {
        "Content-Type: application/json",
        auth_header,
        NULL,
    };

    BedrockStreamCtx stream_ctx = { .cb = cb, .userdata = userdata };
    EventStreamParser *parser = eventstream_parser_create(bedrock_event_handler, &stream_ctx);

    BedrockRawCtx raw_ctx = { .stream_ctx = &stream_ctx, .parser = parser };

    RawStreamRequest raw_req = {
        .url = url,
        .headers = headers,
        .body = body_str,
        .body_len = strlen(body_str),
        .timeout_ms = 120000,
        .on_data = bedrock_raw_data,
        .ctx = &raw_ctx,
    };
    int result = http_stream_raw(&raw_req);

    free(body_str);
    eventstream_parser_free(parser);

    return result;
}

static int bedrock_stream_sigv4(const Model *model, const Message *messages, int msg_count,
                                const char *system_prompt, StreamCallback cb, void *userdata) {
    const char *region = getenv("AWS_REGION");
    if (!region) region = "us-east-1";

    const char *access_key = getenv("AWS_ACCESS_KEY_ID");
    const char *secret_key = getenv("AWS_SECRET_ACCESS_KEY");
    if (!access_key || !secret_key) {
        LOG_ERROR("AWS credentials not set");
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "https://bedrock-runtime.%s.amazonaws.com/model/%s/invoke-with-response-stream",
        region, model->id);

    cJSON *body = build_body(model, messages, msg_count, system_prompt);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    const char *base_headers[] = {
        "Content-Type: application/json",
        NULL,
    };

    const char *session_token = getenv("AWS_SESSION_TOKEN");
    SigV4Request sig_req = {
        .method = "POST",
        .url = url,
        .headers = base_headers,
        .body = body_str,
        .body_len = (int)strlen(body_str),
        .region = region,
        .service = "bedrock",
        .access_key = access_key,
        .secret_key = secret_key,
        .session_token = session_token,
    };

    SigV4Headers signed_hdrs;
    if (sigv4_sign_request(&sig_req, &signed_hdrs) != 0) {
        LOG_ERROR("SigV4 signing failed");
        free(body_str);
        return -1;
    }

    int hdr_count = 0;
    const char **merged_headers = sigv4_merge_headers(base_headers, &signed_hdrs, &hdr_count);

    BedrockStreamCtx ctx = { .cb = cb, .userdata = userdata };

    SSERequest sse_req = {
        .url = url,
        .headers = merged_headers,
        .body = body_str,
        .body_len = strlen(body_str),
        .on_event = bedrock_sse_handler,
        .ctx = &ctx,
    };
    int result = http_stream_sse(&sse_req);

    free(body_str);
    free(merged_headers);
    sigv4_headers_free(&signed_hdrs);

    return result;
}

static int bedrock_stream(const Model *model, const Message *messages, int msg_count,
                          const char *system_prompt, const Tool *tools, int tool_count,
                          const StreamOptions *options,
                          StreamCallback cb, void *userdata) {
    (void)options; (void)tools; (void)tool_count;
    if (!model || !cb) return -1;

    if (get_bedrock_api_key()) {
        return bedrock_stream_apikey(model, messages, msg_count, system_prompt, cb, userdata);
    }
    return bedrock_stream_sigv4(model, messages, msg_count, system_prompt, cb, userdata);
}

static int bedrock_stream_simple(const Model *model, const Message *messages, int msg_count,
                                 const char *system_prompt, const Tool *tools, int tool_count,
                                 const SimpleStreamOptions *options,
                                 StreamCallback cb, void *userdata) {
    (void)options;
    return bedrock_stream(model, messages, msg_count, system_prompt,
                         tools, tool_count, NULL, cb, userdata);
}

static const ApiProvider bedrock_prov = {
    .api = "bedrock",
    .stream = bedrock_stream,
    .stream_simple = bedrock_stream_simple,
};

void bedrock_provider_register(void) {
    ai_register_provider(&bedrock_prov);
}
