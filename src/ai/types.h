#ifndef RIG_AI_TYPES_H
#define RIG_AI_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cjson/cJSON.h"

/* ---- Content Blocks ---- */

typedef enum {
    CONTENT_TEXT,
    CONTENT_THINKING,
    CONTENT_IMAGE,
    CONTENT_TOOL_CALL,
} ContentType;

typedef struct {
    ContentType type;
    union {
        struct { char *text; char *signature; } text;
        struct { char *thinking; char *signature; bool redacted; } thinking;
        struct { char *data; char *mime_type; } image;
        struct { char *id; char *name; cJSON *arguments; char *partial_json; char *thought_signature; } tool_call;
    };
} ContentBlock;

ContentBlock content_text(const char *text, const char *signature);
ContentBlock content_thinking(const char *thinking, const char *signature, bool redacted);
ContentBlock content_image(const char *base64_data, const char *mime_type);
ContentBlock content_tool_call(const char *id, const char *name, cJSON *arguments);
void content_block_free(ContentBlock *b);
ContentBlock content_block_clone(const ContentBlock *b);

/* ---- Messages ---- */

typedef enum {
    ROLE_USER,
    ROLE_ASSISTANT,
    ROLE_TOOL_RESULT,
} MessageRole;

typedef enum {
    STOP_NONE = 0,
    STOP_STOP,
    STOP_LENGTH,
    STOP_TOOL_USE,
    STOP_ERROR,
    STOP_ABORTED,
} StopReason;

typedef struct {
    double input;
    double output;
    double cache_read;
    double cache_write;
    double total;
} CostBreakdown;

typedef struct {
    int input_tokens;
    int output_tokens;
    int cache_read_tokens;
    int cache_write_tokens;
    int total_tokens;
    CostBreakdown cost;
} Usage;

typedef struct {
    MessageRole role;
    ContentBlock *content;
    int content_count;
    int64_t timestamp;

    char *api;
    char *provider;
    char *model_id;
    char *response_id;
    Usage usage;
    StopReason stop_reason;
    char *error_message;

    char *tool_call_id;
    char *tool_name;
    cJSON *details;
    bool is_error;
} Message;

Message *message_create_user(const char *text);
Message *message_create_user_with_images(ContentBlock *content, int count);
Message *message_create_assistant(void);
Message *message_create_tool_result(const char *tool_call_id, const char *tool_name,
                                    ContentBlock *content, int count,
                                    cJSON *details, bool is_error);
void message_free(Message *m);
Message *message_clone(const Message *m);
void message_add_content(Message *m, ContentBlock block);

/* ---- Tool Definition ---- */

typedef enum {
    EXEC_DEFAULT = 0,
    EXEC_SEQUENTIAL,
    EXEC_PARALLEL,
} ToolExecMode;

typedef struct Tool Tool;
struct Tool {
    char *name;
    char *label;
    char *description;
    cJSON *parameters;

    int (*prepare_arguments)(cJSON *args, cJSON **out);
    int (*execute)(const char *call_id, cJSON *params, void *signal,
                   void (*on_update)(void *ctx, cJSON *partial), void *ctx,
                   ContentBlock **content, int *content_count,
                   cJSON **details, bool *terminate);
    ToolExecMode execution_mode;
};

/* ---- Model Definition ---- */

typedef enum {
    COMPAT_NONE = 0,
    COMPAT_OPENAI_COMPLETIONS,
    COMPAT_OPENAI_RESPONSES,
    COMPAT_ANTHROPIC,
} CompatType;

typedef struct {
    bool supports_store;
    bool supports_developer_role;
    bool supports_reasoning_effort;
    bool supports_usage_in_streaming;
    bool supports_strict_mode;
    bool supports_long_cache_retention;
    bool send_session_affinity_headers;
    char *thinking_format;
    char *cache_control_format;
    char *max_tokens_field;
    bool requires_tool_result_name;
    bool requires_assistant_after_tool_result;
    bool requires_thinking_as_text;
    bool requires_reasoning_content_on_assistant;
    bool zai_tool_stream;
    cJSON *open_router_routing;
    cJSON *vercel_gateway_routing;
} OpenAICompletionsCompat;

typedef struct {
    bool send_session_id_header;
    bool supports_long_cache_retention;
} OpenAIResponsesCompat;

typedef struct {
    bool supports_eager_tool_input_streaming;
    bool supports_long_cache_retention;
} AnthropicCompat;

typedef struct {
    char *id;
    char *name;
    char *api;
    char *provider;
    char *base_url;
    bool reasoning;
    const char **input_modalities;
    int input_modality_count;
    struct { double input, output, cache_read, cache_write; } cost_per_million;
    int context_window;
    int max_tokens;
    cJSON *headers;
    CompatType compat_type;
    union {
        OpenAICompletionsCompat openai_completions;
        OpenAIResponsesCompat openai_responses;
        AnthropicCompat anthropic;
    } compat;
} Model;

bool model_supports_images(const Model *m);
double model_calculate_cost(const Model *m, const Usage *usage);
bool model_supports_xhigh(const Model *m);

/* ---- Stream Events ---- */

typedef enum {
    EVENT_START,
    EVENT_TEXT_START,
    EVENT_TEXT_DELTA,
    EVENT_TEXT_END,
    EVENT_THINKING_START,
    EVENT_THINKING_DELTA,
    EVENT_THINKING_END,
    EVENT_TOOLCALL_START,
    EVENT_TOOLCALL_DELTA,
    EVENT_TOOLCALL_END,
    EVENT_DONE,
    EVENT_ERROR,
} StreamEventType;

typedef struct {
    StreamEventType type;
    int content_index;
    char *delta;
    StopReason stop_reason;
    char *error_message;
    Message *partial;
    Message *message;
} StreamEvent;

typedef void (*StreamCallback)(StreamEvent *event, void *userdata);

/* ---- Stream Options ---- */

typedef struct {
    double temperature;
    int max_tokens;
    char *api_key;
    char *transport;
    char *cache_retention;
    char *session_id;
    const char **headers;
    int timeout_ms;
    int max_retries;
    int max_retry_delay_ms;
    cJSON *metadata;
    volatile bool *abort_flag;
} StreamOptions;

typedef enum {
    THINKING_OFF = 0,
    THINKING_MINIMAL,
    THINKING_LOW,
    THINKING_MEDIUM,
    THINKING_HIGH,
    THINKING_XHIGH,
} ThinkingLevel;

typedef struct {
    StreamOptions base;
    ThinkingLevel reasoning;
    cJSON *thinking_budgets;
} SimpleStreamOptions;

#endif
