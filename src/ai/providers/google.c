#include "google.h"
#include "ai/types.h"
#include "util/str.h"
#include "util/http.h"
#include "util/json.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    StreamCallback cb;
    void *userdata;
    Message *partial;
} GoogleStreamCtx;

static void google_sse_handler(const char *event_type, const char *data, void *userdata) {
    GoogleStreamCtx *ctx = (GoogleStreamCtx *)userdata;
    (void)event_type;

    if (!data) return;

    cJSON *json = cJSON_Parse(data);
    if (!json) return;

    cJSON *candidates = cJSON_GetObjectItem(json, "candidates");
    if (candidates && cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON *cand = cJSON_GetArrayItem(candidates, 0);
        cJSON *content = cJSON_GetObjectItem(cand, "content");
        if (content) {
            cJSON *parts = cJSON_GetObjectItem(content, "parts");
            if (parts && cJSON_IsArray(parts)) {
                int part_count = cJSON_GetArraySize(parts);
                for (int i = 0; i < part_count; i++) {
                    cJSON *part = cJSON_GetArrayItem(parts, i);
                    cJSON *text = cJSON_GetObjectItem(part, "text");
                    if (text && cJSON_IsString(text)) {
                        StreamEvent evt = { .type = EVENT_TEXT_DELTA, .delta = text->valuestring };
                        ctx->cb(&evt, ctx->userdata);
                    }
                }
            }
        }

        cJSON *finish = cJSON_GetObjectItem(cand, "finishReason");
        if (finish && cJSON_IsString(finish) && strcmp(finish->valuestring, "STOP") == 0) {
            if (!ctx->partial) {
                ctx->partial = calloc(1, sizeof(Message));
                ctx->partial->role = ROLE_ASSISTANT;
            }
            StreamEvent done = { .type = EVENT_DONE, .message = ctx->partial };
            ctx->cb(&done, ctx->userdata);
        }
    }

    cJSON_Delete(json);
}

static int google_stream(const Model *model, const Message *messages, int msg_count,
                         const char *system_prompt, const Tool *tools, int tool_count,
                         const StreamOptions *options,
                         StreamCallback cb, void *userdata) {
    (void)options; (void)tools; (void)tool_count;
    if (!model || !cb) return -1;

    const char *api_key = getenv("GOOGLE_API_KEY");
    if (!api_key) api_key = getenv("GEMINI_API_KEY");
    if (!api_key) {
        LOG_ERROR("GOOGLE_API_KEY not set");
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent?alt=sse&key=%s",
        model->id, api_key);

    cJSON *body = cJSON_CreateObject();

    cJSON *contents = cJSON_CreateArray();
    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role",
            messages[i].role == ROLE_USER ? "user" : "model");

        cJSON *parts = cJSON_CreateArray();
        for (int j = 0; j < messages[i].content_count; j++) {
            if (messages[i].content[j].type == CONTENT_TEXT) {
                cJSON *part = cJSON_CreateObject();
                cJSON_AddStringToObject(part, "text",
                    messages[i].content[j].text.text ? messages[i].content[j].text.text : "");
                cJSON_AddItemToArray(parts, part);
            }
        }
        cJSON_AddItemToObject(msg, "parts", parts);
        cJSON_AddItemToArray(contents, msg);
    }
    cJSON_AddItemToObject(body, "contents", contents);

    if (system_prompt) {
        cJSON *sys = cJSON_CreateObject();
        cJSON *parts = cJSON_CreateArray();
        cJSON *part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "text", system_prompt);
        cJSON_AddItemToArray(parts, part);
        cJSON_AddItemToObject(sys, "parts", parts);
        cJSON_AddItemToObject(body, "systemInstruction", sys);
    }

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    const char *headers[] = {
        "Content-Type: application/json",
        NULL,
    };

    GoogleStreamCtx ctx = { .cb = cb, .userdata = userdata };

    SSERequest sse_req = { .url = url, .headers = headers, .body = body_str, .body_len = strlen(body_str), .on_event = google_sse_handler, .ctx = &ctx };
    int result = http_stream_sse(&sse_req);

    free(body_str);
    if (ctx.partial) message_free(ctx.partial);

    return result;
}

static int google_stream_simple(const Model *model, const Message *messages, int msg_count,
                                const char *system_prompt, const Tool *tools, int tool_count,
                                const SimpleStreamOptions *options,
                                StreamCallback cb, void *userdata) {
    (void)options;
    return google_stream(model, messages, msg_count, system_prompt,
                        tools, tool_count, NULL, cb, userdata);
}

static const ApiProvider google_provider = {
    .api = "google",
    .stream = google_stream,
    .stream_simple = google_stream_simple,
};

void google_provider_register(void) {
    ai_register_provider(&google_provider);
}
