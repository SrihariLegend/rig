#ifndef RIG_AGENT_H
#define RIG_AGENT_H

#include "ai/types.h"
#include "ai/registry.h"

/* ---- Agent Events ---- */

typedef enum {
    AGENT_EVENT_AGENT_START,
    AGENT_EVENT_AGENT_END,
    AGENT_EVENT_TURN_START,
    AGENT_EVENT_TURN_END,
    AGENT_EVENT_MESSAGE_START,
    AGENT_EVENT_MESSAGE_UPDATE,
    AGENT_EVENT_MESSAGE_END,
    AGENT_EVENT_TOOL_EXEC_START,
    AGENT_EVENT_TOOL_EXEC_UPDATE,
    AGENT_EVENT_TOOL_EXEC_END,
} AgentEventType;

typedef struct {
    AgentEventType type;
    Message *message;
    StreamEvent *stream_event;
    Message **tool_results;
    int tool_results_count;
    char *tool_call_id;
    char *tool_name;
    cJSON *args;
    cJSON *result;
    bool is_error;
} AgentEvent;

typedef void (*AgentEventCallback)(AgentEvent *event, void *userdata);

/* ---- Message Queue ---- */

typedef enum {
    QUEUE_ALL,
    QUEUE_ONE_AT_A_TIME,
} QueueMode;

typedef struct {
    Message **items;
    int count;
    int capacity;
    QueueMode mode;
} MessageQueue;

void queue_init(MessageQueue *q, QueueMode mode);
void queue_enqueue(MessageQueue *q, Message *msg);
int queue_drain(MessageQueue *q, Message ***out, int *count);
bool queue_has_items(const MessageQueue *q);
void queue_clear(MessageQueue *q);
void queue_free(MessageQueue *q);

/* ---- Before/After Tool Call ---- */

typedef struct {
    Message *assistant_message;
    ContentBlock *tool_call;
    cJSON *args;
} BeforeToolCallContext;

typedef struct {
    bool block;
    char *reason;
} BeforeToolCallResult;

typedef struct {
    Message *assistant_message;
    ContentBlock *tool_call;
    cJSON *args;
    ContentBlock *result_content;
    int result_count;
    cJSON *result_details;
    bool is_error;
} AfterToolCallContext;

typedef struct {
    ContentBlock *content;
    int content_count;
    cJSON *details;
    bool is_error;
    bool terminate;
    bool has_overrides;
} AfterToolCallResult;

/* ---- Agent Loop Config ---- */

typedef enum {
    TOOL_EXEC_PARALLEL,
    TOOL_EXEC_SEQUENTIAL,
} ToolExecMode_Config;

typedef struct {
    const Model *model;
    int (*convert_to_llm)(Message **msgs, int count, Message ***out, int *out_count);
    int (*transform_context)(Message **msgs, int count, Message ***out, int *out_count);
    int (*get_api_key)(const char *provider, char **key);
    int (*get_steering_messages)(Message ***out, int *out_count);
    int (*get_follow_up_messages)(Message ***out, int *out_count);
    int (*before_tool_call)(BeforeToolCallContext *ctx, BeforeToolCallResult *result);
    int (*after_tool_call)(AfterToolCallContext *ctx, AfterToolCallResult *result);
    ToolExecMode_Config tool_execution;

    double temperature;
    int max_tokens;
    ThinkingLevel reasoning;
    cJSON *thinking_budgets;
    char *api_key;
    char *transport;
    char *cache_retention;
    char *session_id;
    int timeout_ms;
    volatile bool *abort_flag;
} AgentLoopConfig;

/* ---- Agent State ---- */

typedef struct {
    char *system_prompt;
    const Model *model;
    ThinkingLevel thinking_level;
    Tool *tools;
    int tool_count;

    Message **messages;
    int message_count;
    int message_capacity;

    bool is_streaming;
    Message *streaming_message;
    volatile bool abort_requested;

    MessageQueue steering_queue;
    MessageQueue follow_up_queue;
} AgentState;

AgentState *agent_state_create(void);
void agent_state_free(AgentState *state);
void agent_state_add_message(AgentState *state, Message *msg);
void agent_state_reset(AgentState *state);

/* ---- Public API ---- */

int agent_prompt(AgentState *state, Message **prompts, int prompt_count,
                 AgentLoopConfig *config, AgentEventCallback cb, void *userdata);

int agent_continue(AgentState *state, AgentLoopConfig *config,
                   AgentEventCallback cb, void *userdata);

void agent_abort(AgentState *state);

#endif
