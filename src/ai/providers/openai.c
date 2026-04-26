#include "openai.h"
#include "ai/types.h"
#include "util/str.h"
#include "util/http.h"
#include "util/json.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_URL 512

typedef struct {
    StreamCallback cb;
    void *userdata;
    Message *partial;
    const Model *model;
    Str arg_buffer;
} OpenAIStreamCtx;

static cJSON *build_messages_json(const Message *messages, int msg_count, const char *system_prompt) {
    cJSON *arr = cJSON_CreateArray();

    if (system_prompt) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(arr, sys);
    }

    for (int i = 0; i < msg_count; i++) {
        const Message *m = &messages[i];
        cJSON *msg = cJSON_CreateObject();

        const char *role = "user";
        if (m->role == ROLE_ASSISTANT) role = "assistant";
        else if (m->role == ROLE_TOOL_RESULT) role = "tool";
        cJSON_AddStringToObject(msg, "role", role);

        if (m->content_count == 1 && m->content[0].type == CONTENT_TEXT) {
            cJSON_AddStringToObject(msg, "content",
                m->content[0].text.text ? m->content[0].text.text : "");
        } else if (m->content_count > 0) {
            cJSON *content = cJSON_CreateArray();
            for (int j = 0; j < m->content_count; j++) {
                const ContentBlock *b = &m->content[j];
                if (b->type == CONTENT_TEXT) {
                    cJSON *block = cJSON_CreateObject();
                    cJSON_AddStringToObject(block, "type", "text");
                    cJSON_AddStringToObject(block, "text", b->text.text ? b->text.text : "");
                    cJSON_AddItemToArray(content, block);
                }
            }
            cJSON_AddItemToObject(msg, "content", content);
        }

        bool has_tool_calls = false;
        for (int j = 0; j < m->content_count; j++) {
            if (m->content[j].type == CONTENT_TOOL_CALL) {
                has_tool_calls = true;
                break;
            }
        }

        if (has_tool_calls) {
            cJSON *tool_calls = cJSON_CreateArray();
            for (int j = 0; j < m->content_count; j++) {
                if (m->content[j].type != CONTENT_TOOL_CALL) continue;
                const ContentBlock *b = &m->content[j];
                cJSON *tc = cJSON_CreateObject();
                cJSON_AddStringToObject(tc, "id", b->tool_call.id ? b->tool_call.id : "");
                cJSON_AddStringToObject(tc, "type", "function");
                cJSON *fn = cJSON_CreateObject();
                cJSON_AddStringToObject(fn, "name", b->tool_call.name ? b->tool_call.name : "");
                char *args_str = b->tool_call.arguments ?
                    cJSON_PrintUnformatted(b->tool_call.arguments) : strdup("{}");
                cJSON_AddStringToObject(fn, "arguments", args_str);
                free(args_str);
                cJSON_AddItemToObject(tc, "function", fn);
                cJSON_AddItemToArray(tool_calls, tc);
            }
            cJSON_AddItemToObject(msg, "tool_calls", tool_calls);
        }

        cJSON_AddItemToArray(arr, msg);
    }

    return arr;
}

static cJSON *build_tools_json(const Tool *tools, int tool_count) {
    if (!tools || tool_count <= 0) return NULL;

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", tools[i].name);
        if (tools[i].description) {
            cJSON_AddStringToObject(fn, "description", tools[i].description);
        }
        if (tools[i].parameters) {
            cJSON_AddItemToObject(fn, "parameters",
                cJSON_Duplicate(tools[i].parameters, true));
        }
        cJSON_AddItemToObject(tool, "function", fn);
        cJSON_AddItemToArray(arr, tool);
    }
    return arr;
}

static void openai_sse_handler(const char *event_type, const char *data, void *userdata) {
    OpenAIStreamCtx *ctx = (OpenAIStreamCtx *)userdata;
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
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(json);
        return;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItem(choice, "delta");
    if (!delta) {
        cJSON_Delete(json);
        return;
    }

    if (!ctx->partial) {
        ctx->partial = calloc(1, sizeof(Message));
        ctx->partial->role = ROLE_ASSISTANT;
    }

    cJSON *content = cJSON_GetObjectItem(delta, "content");
    if (content && cJSON_IsString(content) && content->valuestring[0]) {
        StreamEvent evt = {
            .type = EVENT_TEXT_DELTA,
            .delta = content->valuestring,
        };
        ctx->cb(&evt, ctx->userdata);
    }

    cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int tc_count = cJSON_GetArraySize(tool_calls);
        for (int i = 0; i < tc_count; i++) {
            cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
            cJSON *fn = cJSON_GetObjectItem(tc, "function");
            if (!fn) continue;

            cJSON *name = cJSON_GetObjectItem(fn, "name");
            if (name && cJSON_IsString(name)) {
                StreamEvent evt = { .type = EVENT_TOOLCALL_START };
                ctx->cb(&evt, ctx->userdata);
            }

            cJSON *args = cJSON_GetObjectItem(fn, "arguments");
            if (args && cJSON_IsString(args) && args->valuestring[0]) {
                StreamEvent evt = {
                    .type = EVENT_TOOLCALL_DELTA,
                    .delta = args->valuestring,
                };
                ctx->cb(&evt, ctx->userdata);
                str_append(&ctx->arg_buffer, args->valuestring);
            }
        }
    }

    cJSON_Delete(json);
}

static int openai_stream(const Model *model, const Message *messages, int msg_count,
                         const char *system_prompt, const Tool *tools, int tool_count,
                         const StreamOptions *options,
                         StreamCallback cb, void *userdata) {
    (void)options;
    if (!model || !cb) return -1;

    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key) {
        LOG_ERROR("OPENAI_API_KEY not set");
        return -1;
    }

    const char *base_url = model->base_url;
    if (!base_url) base_url = getenv("OPENAI_BASE_URL");
    if (!base_url) base_url = "https://api.openai.com/v1";

    char url[MAX_URL];
    snprintf(url, sizeof(url), "%s/chat/completions", base_url);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", model->id);
    cJSON_AddBoolToObject(body, "stream", 1);

    cJSON *msgs = build_messages_json(messages, msg_count, system_prompt);
    cJSON_AddItemToObject(body, "messages", msgs);

    if (model->max_tokens > 0) {
        cJSON_AddNumberToObject(body, "max_tokens", model->max_tokens);
    }

    cJSON *tools_json = build_tools_json(tools, tool_count);
    if (tools_json) {
        cJSON_AddItemToObject(body, "tools", tools_json);
    }

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);

    const char *headers[] = {
        "Content-Type: application/json",
        auth_header,
        NULL,
    };

    OpenAIStreamCtx ctx = {
        .cb = cb,
        .userdata = userdata,
        .model = model,
        .arg_buffer = str_new(256),
    };

    SSERequest sse_req = { .url = url, .headers = headers, .body = body_str, .body_len = strlen(body_str), .on_event = openai_sse_handler, .ctx = &ctx };
    int result = http_stream_sse(&sse_req);

    free(body_str);
    str_free(&ctx.arg_buffer);
    if (ctx.partial) message_free(ctx.partial);

    return result;
}

static int openai_stream_simple(const Model *model, const Message *messages, int msg_count,
                                const char *system_prompt, const Tool *tools, int tool_count,
                                const SimpleStreamOptions *options,
                                StreamCallback cb, void *userdata) {
    (void)options;
    return openai_stream(model, messages, msg_count, system_prompt,
                        tools, tool_count, NULL, cb, userdata);
}

static const ApiProvider openai_completions = {
    .api = "openai",
    .stream = openai_stream,
    .stream_simple = openai_stream_simple,
};

static const ApiProvider openai_responses_prov = {
    .api = "openai-responses",
    .stream = openai_stream,
    .stream_simple = openai_stream_simple,
};

void openai_completions_register(void) {
    ai_register_provider(&openai_completions);
}

void openai_responses_register(void) {
    ai_register_provider(&openai_responses_prov);
}
