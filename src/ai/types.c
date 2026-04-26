#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *strdup_safe(const char *s) {
    return s ? strdup(s) : NULL;
}

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- Content Block ---- */

ContentBlock content_text(const char *text, const char *signature) {
    return (ContentBlock){
        .type = CONTENT_TEXT,
        .text = { .text = strdup_safe(text), .signature = strdup_safe(signature) },
    };
}

ContentBlock content_thinking(const char *thinking, const char *signature, bool redacted) {
    return (ContentBlock){
        .type = CONTENT_THINKING,
        .thinking = { .thinking = strdup_safe(thinking), .signature = strdup_safe(signature), .redacted = redacted },
    };
}

ContentBlock content_image(const char *base64_data, const char *mime_type) {
    return (ContentBlock){
        .type = CONTENT_IMAGE,
        .image = { .data = strdup_safe(base64_data), .mime_type = strdup_safe(mime_type) },
    };
}

ContentBlock content_tool_call(const char *id, const char *name, cJSON *arguments) {
    return (ContentBlock){
        .type = CONTENT_TOOL_CALL,
        .tool_call = {
            .id = strdup_safe(id),
            .name = strdup_safe(name),
            .arguments = arguments ? cJSON_Duplicate(arguments, 1) : cJSON_CreateObject(),
            .partial_json = NULL,
            .thought_signature = NULL,
        },
    };
}

void content_block_free(ContentBlock *b) {
    if (!b) return;
    switch (b->type) {
    case CONTENT_TEXT:
        free(b->text.text);
        free(b->text.signature);
        break;
    case CONTENT_THINKING:
        free(b->thinking.thinking);
        free(b->thinking.signature);
        break;
    case CONTENT_IMAGE:
        free(b->image.data);
        free(b->image.mime_type);
        break;
    case CONTENT_TOOL_CALL:
        free(b->tool_call.id);
        free(b->tool_call.name);
        cJSON_Delete(b->tool_call.arguments);
        free(b->tool_call.partial_json);
        free(b->tool_call.thought_signature);
        break;
    }
}

ContentBlock content_block_clone(const ContentBlock *b) {
    switch (b->type) {
    case CONTENT_TEXT:
        return content_text(b->text.text, b->text.signature);
    case CONTENT_THINKING:
        return content_thinking(b->thinking.thinking, b->thinking.signature, b->thinking.redacted);
    case CONTENT_IMAGE:
        return content_image(b->image.data, b->image.mime_type);
    case CONTENT_TOOL_CALL: {
        ContentBlock c = content_tool_call(b->tool_call.id, b->tool_call.name, b->tool_call.arguments);
        c.tool_call.partial_json = strdup_safe(b->tool_call.partial_json);
        c.tool_call.thought_signature = strdup_safe(b->tool_call.thought_signature);
        return c;
    }
    }
    return (ContentBlock){0};
}

/* ---- Message ---- */

static Message *message_alloc(MessageRole role) {
    Message *m = calloc(1, sizeof(Message));
    if (!m) return NULL;
    m->role = role;
    m->timestamp = now_ms();
    return m;
}

Message *message_create_user(const char *text) {
    Message *m = message_alloc(ROLE_USER);
    if (!m) return NULL;
    if (text) {
        message_add_content(m, content_text(text, NULL));
    }
    return m;
}

Message *message_create_user_with_images(ContentBlock *content, int count) {
    Message *m = message_alloc(ROLE_USER);
    if (!m) return NULL;
    for (int i = 0; i < count; i++) {
        message_add_content(m, content_block_clone(&content[i]));
    }
    return m;
}

Message *message_create_assistant(void) {
    return message_alloc(ROLE_ASSISTANT);
}

Message *message_create_tool_result(const char *tool_call_id, const char *tool_name,
                                    ContentBlock *content, int count,
                                    cJSON *details, bool is_error) {
    Message *m = message_alloc(ROLE_TOOL_RESULT);
    if (!m) return NULL;
    m->tool_call_id = strdup_safe(tool_call_id);
    m->tool_name = strdup_safe(tool_name);
    m->details = details ? cJSON_Duplicate(details, 1) : NULL;
    m->is_error = is_error;
    for (int i = 0; i < count; i++) {
        message_add_content(m, content_block_clone(&content[i]));
    }
    return m;
}

void message_add_content(Message *m, ContentBlock block) {
    m->content = realloc(m->content, (size_t)(m->content_count + 1) * sizeof(ContentBlock));
    m->content[m->content_count++] = block;
}

void message_free(Message *m) {
    if (!m) return;
    for (int i = 0; i < m->content_count; i++) {
        content_block_free(&m->content[i]);
    }
    free(m->content);
    free(m->api);
    free(m->provider);
    free(m->model_id);
    free(m->response_id);
    free(m->error_message);
    free(m->tool_call_id);
    free(m->tool_name);
    cJSON_Delete(m->details);
    free(m);
}

Message *message_clone(const Message *m) {
    if (!m) return NULL;
    Message *c = calloc(1, sizeof(Message));
    if (!c) return NULL;

    c->role = m->role;
    c->timestamp = m->timestamp;
    c->usage = m->usage;
    c->stop_reason = m->stop_reason;
    c->is_error = m->is_error;

    c->api = strdup_safe(m->api);
    c->provider = strdup_safe(m->provider);
    c->model_id = strdup_safe(m->model_id);
    c->response_id = strdup_safe(m->response_id);
    c->error_message = strdup_safe(m->error_message);
    c->tool_call_id = strdup_safe(m->tool_call_id);
    c->tool_name = strdup_safe(m->tool_name);
    c->details = m->details ? cJSON_Duplicate(m->details, 1) : NULL;

    for (int i = 0; i < m->content_count; i++) {
        message_add_content(c, content_block_clone(&m->content[i]));
    }
    return c;
}

/* ---- Model helpers ---- */

bool model_supports_images(const Model *m) {
    if (!m) return false;
    for (int i = 0; i < m->input_modality_count; i++) {
        if (strcmp(m->input_modalities[i], "image") == 0) return true;
    }
    return false;
}

double model_calculate_cost(const Model *m, const Usage *u) {
    if (!m || !u) return 0.0;
    double cost = 0.0;
    cost += (double)u->input_tokens * m->cost_per_million.input / 1000000.0;
    cost += (double)u->output_tokens * m->cost_per_million.output / 1000000.0;
    cost += (double)u->cache_read_tokens * m->cost_per_million.cache_read / 1000000.0;
    cost += (double)u->cache_write_tokens * m->cost_per_million.cache_write / 1000000.0;
    return cost;
}

bool model_supports_xhigh(const Model *m) {
    if (!m) return false;
    if (strstr(m->id, "opus-4-6") || strstr(m->id, "opus-4-7")) return true;
    if (strstr(m->id, "gpt-5")) return true;
    if (strstr(m->id, "deepseek-v4-pro")) return true;
    return false;
}
