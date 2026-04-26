#include "mistral.h"
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
} MistralStreamCtx;

static void mistral_sse_handler(const char *event_type, const char *data, void *userdata) {
    MistralStreamCtx *ctx = (MistralStreamCtx *)userdata;
    (void)event_type;

    if (!data || strcmp(data, "[DONE]") == 0) {
        if (ctx->partial) {
            StreamEvent done = { .type = EVENT_DONE, .message = ctx->partial };
            ctx->cb(&done, ctx->userdata);
        }
        return;
    }

    cJSON *json = cJSON_Parse(data);
    if (!json) return;

    cJSON *choices = cJSON_GetObjectItem(json, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *delta = cJSON_GetObjectItem(choice, "delta");
        if (delta) {
            cJSON *content = cJSON_GetObjectItem(delta, "content");
            if (content && cJSON_IsString(content) && content->valuestring[0]) {
                if (!ctx->partial) {
                    ctx->partial = calloc(1, sizeof(Message));
                    ctx->partial->role = ROLE_ASSISTANT;
                }
                StreamEvent evt = { .type = EVENT_TEXT_DELTA, .delta = content->valuestring };
                ctx->cb(&evt, ctx->userdata);
            }
        }
    }

    cJSON_Delete(json);
}

static int mistral_stream(const Model *model, const Message *messages, int msg_count,
                          const char *system_prompt, const Tool *tools, int tool_count,
                          const StreamOptions *options,
                          StreamCallback cb, void *userdata) {
    (void)options; (void)tools; (void)tool_count;
    if (!model || !cb) return -1;

    const char *api_key = getenv("MISTRAL_API_KEY");
    if (!api_key) {
        LOG_ERROR("MISTRAL_API_KEY not set");
        return -1;
    }

    const char *base_url = getenv("MISTRAL_BASE_URL");
    if (!base_url) base_url = "https://api.mistral.ai/v1";

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", base_url);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", model->id);
    cJSON_AddBoolToObject(body, "stream", 1);

    cJSON *msgs = cJSON_CreateArray();
    if (system_prompt) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }
    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role",
            messages[i].role == ROLE_USER ? "user" : "assistant");
        if (messages[i].content_count > 0 && messages[i].content[0].type == CONTENT_TEXT) {
            cJSON_AddStringToObject(msg, "content",
                messages[i].content[0].text.text ? messages[i].content[0].text.text : "");
        }
        cJSON_AddItemToArray(msgs, msg);
    }
    cJSON_AddItemToObject(body, "messages", msgs);

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);

    const char *headers[] = {
        "Content-Type: application/json",
        auth,
        NULL,
    };

    MistralStreamCtx ctx = { .cb = cb, .userdata = userdata };

    SSERequest sse_req = { .url = url, .headers = headers, .body = body_str, .body_len = strlen(body_str), .on_event = mistral_sse_handler, .ctx = &ctx };
    int result = http_stream_sse(&sse_req);

    free(body_str);
    if (ctx.partial) message_free(ctx.partial);

    return result;
}

static int mistral_stream_simple(const Model *model, const Message *messages, int msg_count,
                                 const char *system_prompt, const Tool *tools, int tool_count,
                                 const SimpleStreamOptions *options,
                                 StreamCallback cb, void *userdata) {
    (void)options;
    return mistral_stream(model, messages, msg_count, system_prompt,
                         tools, tool_count, NULL, cb, userdata);
}

static const ApiProvider mistral_prov = {
    .api = "mistral",
    .stream = mistral_stream,
    .stream_simple = mistral_stream_simple,
};

void mistral_provider_register(void) {
    ai_register_provider(&mistral_prov);
}
