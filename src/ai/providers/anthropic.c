#include "anthropic.h"
#include "ai/types.h"
#include "ai/json_parse.h"
#include "util/str.h"
#include "util/http.h"
#include "util/json.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ANTHROPIC_API_VERSION "2023-06-01"
#define MAX_URL 512

typedef struct {
    StreamCallback cb;
    void *userdata;
    Message *partial;
    const Model *model;
    int content_index;
} AnthropicStreamCtx;

static cJSON *build_content_blocks(const ContentBlock *content, int count) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        const ContentBlock *b = &content[i];
        cJSON *block = NULL;
        switch (b->type) {
        case CONTENT_TEXT:
            block = cJSON_CreateObject();
            cJSON_AddStringToObject(block, "type", "text");
            cJSON_AddStringToObject(block, "text", b->text.text ? b->text.text : "");
            break;
        case CONTENT_THINKING:
            block = cJSON_CreateObject();
            if (b->thinking.redacted && b->thinking.signature) {
                cJSON_AddStringToObject(block, "type", "redacted_thinking");
                cJSON_AddStringToObject(block, "data", b->thinking.signature);
            } else {
                cJSON_AddStringToObject(block, "type", "thinking");
                cJSON_AddStringToObject(block, "thinking", b->thinking.thinking ? b->thinking.thinking : "");
                if (b->thinking.signature)
                    cJSON_AddStringToObject(block, "signature", b->thinking.signature);
            }
            break;
        case CONTENT_IMAGE:
            block = cJSON_CreateObject();
            cJSON_AddStringToObject(block, "type", "image");
            cJSON *source = cJSON_CreateObject();
            cJSON_AddStringToObject(source, "type", "base64");
            cJSON_AddStringToObject(source, "media_type", b->image.mime_type ? b->image.mime_type : "image/png");
            cJSON_AddStringToObject(source, "data", b->image.data ? b->image.data : "");
            cJSON_AddItemToObject(block, "source", source);
            break;
        case CONTENT_TOOL_CALL:
            block = cJSON_CreateObject();
            cJSON_AddStringToObject(block, "type", "tool_use");
            cJSON_AddStringToObject(block, "id", b->tool_call.id ? b->tool_call.id : "");
            cJSON_AddStringToObject(block, "name", b->tool_call.name ? b->tool_call.name : "");
            cJSON_AddItemToObject(block, "input", b->tool_call.arguments ?
                                  cJSON_Duplicate(b->tool_call.arguments, 1) : cJSON_CreateObject());
            break;
        }
        if (block) cJSON_AddItemToArray(arr, block);
    }
    return arr;
}

static cJSON *build_messages_json(const Message *messages, int msg_count) {
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < msg_count; i++) {
        const Message *m = &messages[i];

        if (m->role == ROLE_ASSISTANT && (m->stop_reason == STOP_ERROR || m->stop_reason == STOP_ABORTED))
            continue;

        cJSON *msg = cJSON_CreateObject();

        if (m->role == ROLE_USER) {
            cJSON_AddStringToObject(msg, "role", "user");
            if (m->content_count == 1 && m->content[0].type == CONTENT_TEXT) {
                cJSON_AddStringToObject(msg, "content", m->content[0].text.text);
            } else {
                cJSON_AddItemToObject(msg, "content", build_content_blocks(m->content, m->content_count));
            }
        } else if (m->role == ROLE_ASSISTANT) {
            cJSON_AddStringToObject(msg, "role", "assistant");
            cJSON_AddItemToObject(msg, "content", build_content_blocks(m->content, m->content_count));
        } else if (m->role == ROLE_TOOL_RESULT) {
            cJSON_AddStringToObject(msg, "role", "user");
            cJSON *content = cJSON_CreateArray();
            cJSON *tr = cJSON_CreateObject();
            cJSON_AddStringToObject(tr, "type", "tool_result");
            cJSON_AddStringToObject(tr, "tool_use_id", m->tool_call_id ? m->tool_call_id : "");
            if (m->is_error) cJSON_AddBoolToObject(tr, "is_error", 1);
            if (m->content_count > 0 && m->content[0].type == CONTENT_TEXT) {
                cJSON_AddStringToObject(tr, "content", m->content[0].text.text);
            } else if (m->content_count > 0) {
                cJSON_AddItemToObject(tr, "content", build_content_blocks(m->content, m->content_count));
            }
            cJSON_AddItemToArray(content, tr);
            cJSON_AddItemToObject(msg, "content", content);
        }

        cJSON_AddItemToArray(arr, msg);
    }
    return arr;
}

static cJSON *build_tools_json(const Tool *tools, int tool_count) {
    if (!tools || tool_count == 0) return NULL;
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < tool_count; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", tools[i].name);
        cJSON_AddStringToObject(t, "description", tools[i].description ? tools[i].description : "");
        cJSON_AddItemToObject(t, "input_schema", tools[i].parameters ?
                              cJSON_Duplicate(tools[i].parameters, 1) : cJSON_CreateObject());
        cJSON_AddItemToArray(arr, t);
    }
    return arr;
}

static cJSON *build_thinking_config(const Model *model, ThinkingLevel level, cJSON *budgets) {
    if (level == THINKING_OFF) return NULL;

    cJSON *thinking = cJSON_CreateObject();

    const char *effort_map[] = { NULL, "low", "low", "medium", "high", "max" };
    const char *effort = (level <= THINKING_XHIGH) ? effort_map[level] : "medium";

    if (strstr(model->id, "opus-4-6") || strstr(model->id, "sonnet-4-6") ||
        strstr(model->id, "opus-4-7")) {
        cJSON_AddStringToObject(thinking, "type", "adaptive");
        cJSON_AddStringToObject(thinking, "display", "summarized");
        if (effort) cJSON_AddStringToObject(thinking, "effort", effort);
    } else {
        cJSON_AddStringToObject(thinking, "type", "enabled");
        int budget = 10000;
        if (budgets) {
            const char *level_names[] = { NULL, "minimal", "low", "medium", "high" };
            if (level <= THINKING_HIGH) {
                cJSON *b = cJSON_GetObjectItem(budgets, level_names[level]);
                if (b && cJSON_IsNumber(b)) budget = b->valueint;
            }
        }
        cJSON_AddNumberToObject(thinking, "budget_tokens", budget);
    }

    return thinking;
}

static cJSON *build_request_body(const Model *model, const Message *messages, int msg_count,
                                  const char *system_prompt, const Tool *tools, int tool_count,
                                  const SimpleStreamOptions *options) {
    cJSON *body = cJSON_CreateObject();

    cJSON_AddStringToObject(body, "model", model->id);
    cJSON_AddNumberToObject(body, "max_tokens", options->base.max_tokens > 0 ?
                            options->base.max_tokens : model->max_tokens);
    cJSON_AddBoolToObject(body, "stream", 1);

    if (system_prompt && *system_prompt) {
        cJSON_AddStringToObject(body, "system", system_prompt);
    }

    if (options->base.temperature >= 0) {
        cJSON_AddNumberToObject(body, "temperature", options->base.temperature);
    }

    cJSON_AddItemToObject(body, "messages", build_messages_json(messages, msg_count));

    cJSON *tools_json = build_tools_json(tools, tool_count);
    if (tools_json) cJSON_AddItemToObject(body, "tools", tools_json);

    cJSON *thinking = build_thinking_config(model, options->reasoning, options->thinking_budgets);
    if (thinking) cJSON_AddItemToObject(body, "thinking", thinking);

    return body;
}

static void emit_event(AnthropicStreamCtx *ctx, StreamEventType type) {
    StreamEvent ev = {
        .type = type,
        .content_index = ctx->content_index,
        .partial = ctx->partial,
    };
    ctx->cb(&ev, ctx->userdata);
}

static void emit_delta(AnthropicStreamCtx *ctx, StreamEventType type, const char *delta) {
    StreamEvent ev = {
        .type = type,
        .content_index = ctx->content_index,
        .delta = (char *)delta,
        .partial = ctx->partial,
    };
    ctx->cb(&ev, ctx->userdata);
}

static void emit_done(AnthropicStreamCtx *ctx, StopReason reason) {
    ctx->partial->stop_reason = reason;
    StreamEvent ev = {
        .type = EVENT_DONE,
        .stop_reason = reason,
        .message = ctx->partial,
    };
    ctx->cb(&ev, ctx->userdata);
}

static void emit_error(AnthropicStreamCtx *ctx, const char *error_msg) {
    ctx->partial->stop_reason = STOP_ERROR;
    ctx->partial->error_message = strdup(error_msg);
    StreamEvent ev = {
        .type = EVENT_ERROR,
        .stop_reason = STOP_ERROR,
        .error_message = (char *)error_msg,
        .message = ctx->partial,
    };
    ctx->cb(&ev, ctx->userdata);
}

static void handle_sse_event(const char *event_type, const char *data, void *userdata) {
    AnthropicStreamCtx *ctx = userdata;

    cJSON *json = cJSON_Parse(data);
    if (!json) return;

    if (strcmp(event_type, "message_start") == 0) {
        cJSON *msg = cJSON_GetObjectItem(json, "message");
        if (msg) {
            cJSON *id = cJSON_GetObjectItem(msg, "id");
            if (id && cJSON_IsString(id)) {
                free(ctx->partial->response_id);
                ctx->partial->response_id = strdup(id->valuestring);
            }
            cJSON *usage = cJSON_GetObjectItem(msg, "usage");
            if (usage) {
                ctx->partial->usage.input_tokens = json_get_int(usage, "input_tokens", 0);
                ctx->partial->usage.cache_read_tokens = json_get_int(usage, "cache_read_tokens", 0);
                ctx->partial->usage.cache_write_tokens = json_get_int(usage, "cache_creation_input_tokens", 0);
            }
        }
        ctx->partial->api = strdup("anthropic-messages");
        ctx->partial->provider = strdup(ctx->model->provider);
        ctx->partial->model_id = strdup(ctx->model->id);
        emit_event(ctx, EVENT_START);
    }
    else if (strcmp(event_type, "content_block_start") == 0) {
        cJSON *cb_json = cJSON_GetObjectItem(json, "content_block");
        cJSON *index = cJSON_GetObjectItem(json, "index");
        if (index) ctx->content_index = index->valueint;

        if (cb_json) {
            const char *type = json_get_string(cb_json, "type");
            if (type && strcmp(type, "text") == 0) {
                message_add_content(ctx->partial, content_text("", NULL));
                emit_event(ctx, EVENT_TEXT_START);
            } else if (type && strcmp(type, "thinking") == 0) {
                message_add_content(ctx->partial, content_thinking("", NULL, false));
                emit_event(ctx, EVENT_THINKING_START);
            } else if (type && strcmp(type, "tool_use") == 0) {
                const char *id = json_get_string(cb_json, "id");
                const char *name = json_get_string(cb_json, "name");
                message_add_content(ctx->partial, content_tool_call(id, name, NULL));
                emit_event(ctx, EVENT_TOOLCALL_START);
            }
        }
    }
    else if (strcmp(event_type, "content_block_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(json, "delta");
        cJSON *index = cJSON_GetObjectItem(json, "index");
        if (index) ctx->content_index = index->valueint;

        if (delta) {
            const char *type = json_get_string(delta, "type");
            if (type && strcmp(type, "text_delta") == 0) {
                const char *text = json_get_string(delta, "text");
                if (text && ctx->content_index < ctx->partial->content_count) {
                    ContentBlock *b = &ctx->partial->content[ctx->content_index];
                    size_t old_len = b->text.text ? strlen(b->text.text) : 0;
                    size_t delta_len = strlen(text);
                    b->text.text = realloc(b->text.text, old_len + delta_len + 1);
                    memcpy(b->text.text + old_len, text, delta_len + 1);
                    emit_delta(ctx, EVENT_TEXT_DELTA, text);
                }
            } else if (type && strcmp(type, "thinking_delta") == 0) {
                const char *thinking = json_get_string(delta, "thinking");
                if (thinking && ctx->content_index < ctx->partial->content_count) {
                    ContentBlock *b = &ctx->partial->content[ctx->content_index];
                    size_t old_len = b->thinking.thinking ? strlen(b->thinking.thinking) : 0;
                    size_t delta_len = strlen(thinking);
                    b->thinking.thinking = realloc(b->thinking.thinking, old_len + delta_len + 1);
                    memcpy(b->thinking.thinking + old_len, thinking, delta_len + 1);
                    emit_delta(ctx, EVENT_THINKING_DELTA, thinking);
                }
            } else if (type && strcmp(type, "input_json_delta") == 0) {
                const char *pjson = json_get_string(delta, "partial_json");
                if (pjson && ctx->content_index < ctx->partial->content_count) {
                    ContentBlock *b = &ctx->partial->content[ctx->content_index];
                    size_t old_len = b->tool_call.partial_json ? strlen(b->tool_call.partial_json) : 0;
                    size_t delta_len = strlen(pjson);
                    b->tool_call.partial_json = realloc(b->tool_call.partial_json, old_len + delta_len + 1);
                    memcpy(b->tool_call.partial_json + old_len, pjson, delta_len + 1);

                    cJSON *parsed = json_parse_streaming(b->tool_call.partial_json);
                    if (parsed) {
                        cJSON_Delete(b->tool_call.arguments);
                        b->tool_call.arguments = parsed;
                    }
                    emit_delta(ctx, EVENT_TOOLCALL_DELTA, pjson);
                }
            }
        }
    }
    else if (strcmp(event_type, "content_block_stop") == 0) {
        cJSON *index = cJSON_GetObjectItem(json, "index");
        if (index) ctx->content_index = index->valueint;

        if (ctx->content_index < ctx->partial->content_count) {
            ContentBlock *b = &ctx->partial->content[ctx->content_index];
            switch (b->type) {
            case CONTENT_TEXT: emit_event(ctx, EVENT_TEXT_END); break;
            case CONTENT_THINKING: emit_event(ctx, EVENT_THINKING_END); break;
            case CONTENT_TOOL_CALL: emit_event(ctx, EVENT_TOOLCALL_END); break;
            default: break;
            }
        }
    }
    else if (strcmp(event_type, "message_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(json, "delta");
        if (delta) {
            const char *stop = json_get_string(delta, "stop_reason");
            if (stop) {
                StopReason reason = STOP_STOP;
                if (strcmp(stop, "end_turn") == 0 || strcmp(stop, "stop") == 0) reason = STOP_STOP;
                else if (strcmp(stop, "max_tokens") == 0) reason = STOP_LENGTH;
                else if (strcmp(stop, "tool_use") == 0) reason = STOP_TOOL_USE;
                emit_done(ctx, reason);
            }
        }
        cJSON *usage = cJSON_GetObjectItem(json, "usage");
        if (usage) {
            ctx->partial->usage.output_tokens = json_get_int(usage, "output_tokens", 0);
        }
    }
    else if (strcmp(event_type, "error") == 0) {
        cJSON *err = cJSON_GetObjectItem(json, "error");
        const char *msg = err ? json_get_string(err, "message") : "Unknown error";
        emit_error(ctx, msg ? msg : "Unknown error");
    }

    cJSON_Delete(json);
}

int anthropic_stream_simple(const Model *model, const Message *messages, int msg_count,
                            const char *system_prompt, const Tool *tools, int tool_count,
                            const SimpleStreamOptions *options, StreamCallback cb, void *userdata) {
    if (!model || !cb) return -1;

    cJSON *body = build_request_body(model, messages, msg_count, system_prompt,
                                     tools, tool_count, options);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return -1;

    Message *partial = message_create_assistant();
    if (!partial) { free(body_str); return -1; }

    AnthropicStreamCtx ctx = {
        .cb = cb,
        .userdata = userdata,
        .partial = partial,
        .model = model,
        .content_index = 0,
    };

    char url[MAX_URL];
    snprintf(url, MAX_URL, "%s/v1/messages", model->base_url);

    bool is_oauth = options->base.api_key && strncmp(options->base.api_key, "sk-ant-oat", 10) == 0;

    Str auth_header = str_new(128);
    if (is_oauth) {
        str_appendf(&auth_header, "Authorization: Bearer %s", options->base.api_key);
    } else {
        str_appendf(&auth_header, "x-api-key: %s", options->base.api_key ? options->base.api_key : "");
    }

    const char *headers[] = {
        "Content-Type: application/json",
        "anthropic-version: " ANTHROPIC_API_VERSION,
        auth_header.data,
        NULL,
    };

    SSERequest req = {
        .url = url,
        .headers = headers,
        .body = body_str,
        .body_len = strlen(body_str),
        .timeout_ms = options->base.timeout_ms,
        .on_event = handle_sse_event,
        .ctx = &ctx,
        .abort_flag = options->base.abort_flag,
    };

    int result = http_stream_sse(&req);

    if (result != 0 && partial->stop_reason == STOP_NONE) {
        emit_error(&ctx, "HTTP request failed");
    }

    str_free(&auth_header);
    free(body_str);

    partial->usage.total_tokens = partial->usage.input_tokens + partial->usage.output_tokens +
                                   partial->usage.cache_read_tokens + partial->usage.cache_write_tokens;
    partial->usage.cost = (CostBreakdown){
        .input = (double)partial->usage.input_tokens * model->cost_per_million.input / 1000000.0,
        .output = (double)partial->usage.output_tokens * model->cost_per_million.output / 1000000.0,
        .cache_read = (double)partial->usage.cache_read_tokens * model->cost_per_million.cache_read / 1000000.0,
        .cache_write = (double)partial->usage.cache_write_tokens * model->cost_per_million.cache_write / 1000000.0,
    };
    partial->usage.cost.total = partial->usage.cost.input + partial->usage.cost.output +
                                 partial->usage.cost.cache_read + partial->usage.cost.cache_write;

    return result;
}

void anthropic_register(void) {
    static ApiProvider p = {
        .api = "anthropic-messages",
        .stream = NULL,
        .stream_simple = anthropic_stream_simple,
    };
    ai_register_provider(&p);
}
