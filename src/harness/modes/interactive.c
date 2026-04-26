#include "interactive.h"
#include "pi.h"
#include "harness/tools/tools.h"
#include "harness/model_registry.h"
#include "ai/providers/anthropic.h"
#include "ai/providers/openai.h"
#include "ai/providers/google.h"
#include "ai/providers/bedrock.h"
#include "ai/providers/mistral.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

/* ---- Constants ---- */

#define MAX_HISTORY_WIDGETS 256
#define STATUS_BUF_SIZE     256

/* ---- Interactive State ---- */

typedef enum {
    ISTATE_IDLE,
    ISTATE_STREAMING,
} InteractivePhase;

typedef struct {
    PiInstance *pi;
    TUI *tui;
    Session *session;
    AgentState *agent;
    AgentLoopConfig agent_config;

    /* Layout components */
    Component *history_widgets[MAX_HISTORY_WIDGETS];
    int history_count;
    Component *status_line;
    Component *loader;
    Component *input;

    /* Streaming state */
    InteractivePhase phase;
    Str current_assistant_text;
    Component *current_assistant_widget;
    bool thinking_active;
    char *current_tool_name;

    /* Model info */
    const Model *model;
    int total_tokens;
    int scroll_offset;

    /* Tools */
    Tool tools[6];
    int tool_count;
    char cwd[4096];
    char *api_key;
} InteractiveState;

/* ---- Forward Declarations ---- */

static void rebuild_tui_components(InteractiveState *state);
static void add_history_widget(InteractiveState *state, const char *role_prefix, const char *text);
static void update_status_line(InteractiveState *state);
static void on_agent_event(AgentEvent *event, void *userdata);
static void handle_submit(InteractiveState *state);
static void *agent_thread_fn(void *arg);
static bool interactive_key_handler(TUI *tui, const ParsedKey *key, void *ctx);

/* ---- TUI Component Management ---- */

static void rebuild_tui_components(InteractiveState *state) {
    /* Remove all components from TUI (don't free them, we manage lifecycle) */
    while (state->tui->component_count > 0) {
        tui_remove_component(state->tui, state->tui->components[0]);
    }

    /* Add history widgets */
    for (int i = 0; i < state->history_count; i++) {
        tui_add_component(state->tui, state->history_widgets[i]);
    }

    /* Add loader if streaming */
    if (state->phase == ISTATE_STREAMING && state->loader) {
        tui_add_component(state->tui, state->loader);
    }

    /* Add status line */
    if (state->status_line) {
        tui_add_component(state->tui, state->status_line);
    }

    /* Add input widget */
    if (state->input) {
        tui_add_component(state->tui, state->input);
    }

    tui_invalidate(state->tui);
}

static void add_history_widget(InteractiveState *state, const char *role_prefix, const char *text) {
    if (state->history_count >= MAX_HISTORY_WIDGETS) return;

    Str formatted = str_new(256);
    if (role_prefix) {
        str_appendf(&formatted, "\x1b[1m%s\x1b[0m %s", role_prefix, text ? text : "");
    } else {
        str_append(&formatted, text ? text : "");
    }

    Component *widget = widget_text_create(formatted.data);
    str_free(&formatted);

    if (!widget) return;

    state->history_widgets[state->history_count++] = widget;
    rebuild_tui_components(state);
}

static void update_status_line(InteractiveState *state) {
    Str status = str_new(STATUS_BUF_SIZE);

    const char *model_name = state->model ? state->model->name : "unknown";
    str_appendf(&status, "\x1b[2m%s", model_name);

    if (state->total_tokens > 0) {
        str_appendf(&status, " | %d tokens", state->total_tokens);
    }

    if (state->thinking_active) {
        str_append(&status, " | thinking...");
    }

    if (state->current_tool_name) {
        str_appendf(&status, " | using: %s", state->current_tool_name);
    }

    str_append(&status, "\x1b[0m");

    widget_text_set(state->status_line, status.data);
    str_free(&status);
    tui_invalidate(state->tui);
}

/* ---- Agent Event Callback ---- */

static void on_agent_event(AgentEvent *event, void *userdata) {
    InteractiveState *state = userdata;
    if (!state || !state->tui) return;

    switch (event->type) {
    case AGENT_EVENT_MESSAGE_START:
        /* A new assistant message is starting */
        state->current_assistant_text = str_new(1024);
        if (state->history_count < MAX_HISTORY_WIDGETS) {
            Component *widget = widget_text_create("");
            state->current_assistant_widget = widget;
            state->history_widgets[state->history_count++] = widget;
        }
        break;

    case AGENT_EVENT_MESSAGE_UPDATE:
        if (!event->stream_event) break;
        switch (event->stream_event->type) {
        case EVENT_TEXT_DELTA:
            if (event->stream_event->delta && state->current_assistant_widget) {
                str_append(&state->current_assistant_text, event->stream_event->delta);

                Str display = str_new(state->current_assistant_text.len + 32);
                str_appendf(&display, "\x1b[36m>\x1b[0m %s", state->current_assistant_text.data);
                widget_text_set(state->current_assistant_widget, display.data);
                str_free(&display);

                rebuild_tui_components(state);
            }
            break;

        case EVENT_THINKING_START:
            state->thinking_active = true;
            update_status_line(state);
            break;

        case EVENT_THINKING_DELTA:
            /* Thinking content - just keep the indicator active */
            break;

        case EVENT_THINKING_END:
            state->thinking_active = false;
            update_status_line(state);
            break;

        case EVENT_TOOLCALL_START:
            if (event->stream_event->delta) {
                free(state->current_tool_name);
                state->current_tool_name = strdup(event->stream_event->delta);
            } else if (event->stream_event->partial &&
                       event->stream_event->content_index >= 0 &&
                       event->stream_event->content_index < event->stream_event->partial->content_count) {
                ContentBlock *blk = &event->stream_event->partial->content[event->stream_event->content_index];
                if (blk->type == CONTENT_TOOL_CALL && blk->tool_call.name) {
                    free(state->current_tool_name);
                    state->current_tool_name = strdup(blk->tool_call.name);
                }
            }
            update_status_line(state);
            break;

        case EVENT_TOOLCALL_END:
            free(state->current_tool_name);
            state->current_tool_name = NULL;
            update_status_line(state);
            break;

        case EVENT_DONE:
            /* Finalize the message */
            state->phase = ISTATE_IDLE;
            state->thinking_active = false;
            free(state->current_tool_name);
            state->current_tool_name = NULL;

            if (event->stream_event->message) {
                state->total_tokens += event->stream_event->message->usage.total_tokens;
            }

            update_status_line(state);
            rebuild_tui_components(state);
            break;

        case EVENT_ERROR:
            state->phase = ISTATE_IDLE;
            if (event->stream_event->error_message) {
                add_history_widget(state, "\x1b[31mError:\x1b[0m",
                                   event->stream_event->error_message);
            }
            rebuild_tui_components(state);
            break;

        default:
            break;
        }
        break;

    case AGENT_EVENT_MESSAGE_END:
        if (event->message && event->message->role == ROLE_ASSISTANT) {
            /* Persist to session */
            if (state->session) {
                cJSON *msg_data = cJSON_CreateObject();
                cJSON_AddStringToObject(msg_data, "role", "assistant");
                Str full_text = str_new(256);
                for (int i = 0; i < event->message->content_count; i++) {
                    if (event->message->content[i].type == CONTENT_TEXT) {
                        str_append(&full_text, event->message->content[i].text.text);
                    }
                }
                cJSON_AddStringToObject(msg_data, "text", full_text.data);
                session_append(state->session, ENTRY_MESSAGE, msg_data);
                session_flush(state->session);
                str_free(&full_text);
            }
        }
        str_free(&state->current_assistant_text);
        state->current_assistant_widget = NULL;
        break;

    case AGENT_EVENT_TOOL_EXEC_START:
        if (event->tool_name) {
            Str tool_msg = str_new(128);
            str_appendf(&tool_msg, "\x1b[33mUsing tool: %s\x1b[0m", event->tool_name);
            add_history_widget(state, NULL, tool_msg.data);
            str_free(&tool_msg);
        }
        break;

    case AGENT_EVENT_TOOL_EXEC_END:
        /* Tool execution finished */
        break;

    default:
        break;
    }
}

/* ---- Input Handling ---- */

static void handle_submit(InteractiveState *state) {
    const char *text = widget_input_get_text(state->input);
    if (!text || !text[0]) return;

    if (!state->model || !state->api_key) {
        add_history_widget(state, "\x1b[31mError:\x1b[0m",
            "No API key configured. Set ANTHROPIC_API_KEY, OPENAI_API_KEY, etc.");
        widget_input_clear(state->input);
        return;
    }

    char *prompt_text = strdup(text);
    widget_input_clear(state->input);

    /* Display user message */
    add_history_widget(state, "\x1b[32m>\x1b[0m", prompt_text);

    /* Persist user message to session */
    if (state->session) {
        cJSON *msg_data = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_data, "role", "user");
        cJSON_AddStringToObject(msg_data, "text", prompt_text);
        session_append(state->session, ENTRY_MESSAGE, msg_data);
        session_flush(state->session);
    }

    /* Show loader */
    state->phase = ISTATE_STREAMING;
    if (state->loader) {
        widget_loader_tick(state->loader);
    }
    rebuild_tui_components(state);

    /* Run agent in background thread so TUI stays responsive */
    typedef struct { InteractiveState *state; char *prompt; } AgentThreadArg;
    AgentThreadArg *arg = malloc(sizeof(AgentThreadArg));
    arg->state = state;
    arg->prompt = prompt_text;

    pthread_t tid;
    pthread_create(&tid, NULL, agent_thread_fn, arg);
    pthread_detach(tid);
}

static void *agent_thread_fn(void *raw_arg) {
    typedef struct { InteractiveState *state; char *prompt; } AgentThreadArg;
    AgentThreadArg *arg = raw_arg;
    InteractiveState *state = arg->state;
    char *prompt_text = arg->prompt;
    free(arg);

    Message *prompt_msg = message_create_user(prompt_text);
    agent_prompt(state->agent, &prompt_msg, 1, &state->agent_config,
                 on_agent_event, state);

    state->phase = ISTATE_IDLE;
    rebuild_tui_components(state);

    free(prompt_text);
    return NULL;
}

static bool interactive_key_handler(TUI *tui, const ParsedKey *key, void *ctx) {
    InteractiveState *state = ctx;

    if (key_matches(key, "enter")) {
        if (state->phase == ISTATE_IDLE) {
            handle_submit(state);
        }
        return true;
    }

    if (key_matches(key, "ctrl+c")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            add_history_widget(state, NULL, "\x1b[2m(generation aborted)\x1b[0m");
            rebuild_tui_components(state);
        } else {
            tui_quit(tui);
        }
        return true;
    }

    if (key_matches(key, "escape")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            add_history_widget(state, NULL, "\x1b[2m(generation cancelled)\x1b[0m");
            rebuild_tui_components(state);
        }
        return true;
    }

    if (key_matches(key, "ctrl+l")) {
        tui_render_full(tui);
        return true;
    }

    /* Let input widget handle all other keys */
    if (state->input && state->input->handle_input) {
        const char *raw = key->printable[0] ? key->printable : key->id;
        state->input->handle_input(state->input, raw, (int)strlen(raw));
        tui_invalidate(tui);
    }
    return true;
}

/* ---- Session Restore ---- */

static void restore_session_history(InteractiveState *state) {
    if (!state->session) return;

    Message **messages = NULL;
    int count = 0;
    if (session_build_context(state->session, &messages, &count) != 0) return;

    for (int i = 0; i < count; i++) {
        Message *msg = messages[i];
        Str text = str_new(256);

        for (int j = 0; j < msg->content_count; j++) {
            if (msg->content[j].type == CONTENT_TEXT && msg->content[j].text.text) {
                str_append(&text, msg->content[j].text.text);
            }
        }

        if (msg->role == ROLE_USER) {
            add_history_widget(state, "\x1b[32m>\x1b[0m", text.data);
        } else if (msg->role == ROLE_ASSISTANT) {
            Str display = str_new(text.len + 32);
            str_appendf(&display, "\x1b[36m>\x1b[0m %s", text.data);
            add_history_widget(state, NULL, display.data);
            str_free(&display);
        }

        /* Feed restored messages to agent state */
        agent_state_add_message(state->agent, message_clone(msg));

        str_free(&text);
    }

    /* Free the messages array (clones were made for agent) */
    for (int i = 0; i < count; i++) {
        message_free(messages[i]);
    }
    free(messages);
}

/* ---- Public API ---- */

InteractiveState *interactive_state_create(PiInstance *pi, const Model *model, const char *api_key) {
    InteractiveState *state = calloc(1, sizeof(InteractiveState));
    if (!state) return NULL;

    state->pi = pi;
    state->model = model;
    state->api_key = api_key ? strdup(api_key) : NULL;
    state->phase = ISTATE_IDLE;

    /* Create agent state */
    state->agent = agent_state_create();
    if (!state->agent) {
        free(state->api_key);
        free(state);
        return NULL;
    }
    state->agent->model = model;
    state->agent->thinking_level = THINKING_OFF;

    /* Set up tools */
    getcwd(state->cwd, sizeof(state->cwd));
    state->tools[state->tool_count++] = tool_bash_create(state->cwd);
    state->tools[state->tool_count++] = tool_read_create();
    state->tools[state->tool_count++] = tool_write_create();
    state->tools[state->tool_count++] = tool_edit_create();
    state->tools[state->tool_count++] = tool_grep_create();
    state->tools[state->tool_count++] = tool_ls_create();

    state->agent->tools = state->tools;
    state->agent->tool_count = state->tool_count;

    /* Build system prompt */
    state->agent->system_prompt = system_prompt_build(
        state->tools, state->tool_count, state->cwd);

    /* Agent loop config */
    state->agent_config = (AgentLoopConfig){
        .model = model,
        .tool_execution = TOOL_EXEC_PARALLEL,
        .temperature = -1,
        .max_tokens = model ? model->max_tokens : 4096,
        .reasoning = THINKING_OFF,
        .api_key = state->api_key,
        .timeout_ms = 120000,
        .abort_flag = &state->agent->abort_requested,
    };

    /* Create TUI widgets */
    state->status_line = widget_text_create("");
    state->loader = widget_loader_create("Thinking...");
    state->input = widget_input_create("Type a message...");

    return state;
}

void interactive_state_free(InteractiveState *state) {
    if (!state) return;

    /* Free history widgets */
    for (int i = 0; i < state->history_count; i++) {
        component_free(state->history_widgets[i]);
    }

    if (state->status_line) component_free(state->status_line);
    if (state->loader) component_free(state->loader);
    if (state->input) component_free(state->input);

    if (state->agent) agent_state_free(state->agent);
    if (state->session) session_free(state->session);

    free(state->current_tool_name);
    free(state->api_key);
    free(state);
}

int interactive_mode_start(PiInstance *pi, const char *session_id,
                           const char *model_pattern, const char *provider) {
    if (!pi) return -1;

    /* Initialize subsystems */
    http_global_init();
    ai_registry_init();
    models_init();
    anthropic_register();
    openai_completions_register();
    openai_responses_register();
    google_provider_register();
    bedrock_provider_register();
    mistral_provider_register();

    /* Resolve model: explicit pattern, or first available with API key (alphabetical) */
    const Model *model = NULL;
    char *api_key = NULL;

    if (model_pattern) {
        model = models_get(provider, model_pattern);
        if (model) api_key = auth_get_api_key(model->provider);
    } else {
        int all_count = 0;
        const Model **all_models = models_get_all(provider, &all_count);
        for (int i = 0; i < all_count && !api_key; i++) {
            char *key = auth_get_api_key(all_models[i]->provider);
            if (key) { model = all_models[i]; api_key = key; }
        }
    }

    /* Create interactive state (model/api_key may be NULL) */
    InteractiveState *state = interactive_state_create(pi, model, api_key);
    free(api_key);
    if (!state) return -1;

    /* Create or resume session */
    const char *sessions_dir = config_sessions_dir();
    if (session_id) {
        /* Try to load existing session */
        Str path = str_new(256);
        str_appendf(&path, "%s/%s.jsonl", sessions_dir, session_id);
        state->session = session_load(path.data);
        str_free(&path);

        if (state->session) {
            restore_session_history(state);
        } else {
            LOG_WARN("Session '%s' not found, creating new session", session_id);
            state->session = session_create(sessions_dir);
        }
    } else {
        state->session = session_create(sessions_dir);
    }

    /* Set up TUI */
    state->tui = pi->tui ? pi->tui : tui_create();
    if (!state->tui) {
        interactive_state_free(state);
        return -1;
    }

    if (!pi->tui) {
        pi->tui = state->tui;
    }

    /* Set key handler - this overrides tui_run's default ctrl+c/escape handling */
    tui_set_key_handler(state->tui, interactive_key_handler, state);

    /* If no provider configured, show welcome message with setup instructions */
    if (!model || !state->api_key) {
        add_history_widget(state, NULL,
            "\x1b[1mWelcome to Pi\x1b[0m");
        add_history_widget(state, NULL,
            "\x1b[33mNo API key / provider configured.\x1b[0m\n"
            "Set one of these environment variables:\n"
            "  ANTHROPIC_API_KEY    (Claude models)\n"
            "  OPENAI_API_KEY       (GPT models)\n"
            "  GOOGLE_API_KEY       (Gemini models)\n"
            "  MISTRAL_API_KEY      (Mistral models)\n"
            "  AWS_ACCESS_KEY_ID    (Bedrock models)\n\n"
            "Then restart pi. Press Ctrl+C to exit.");
    }

    /* Build initial layout */
    update_status_line(state);
    rebuild_tui_components(state);

    /* Run the TUI event loop */
    int rc = tui_run(state->tui);

    /* Cleanup - detach widgets from TUI before freeing */
    while (state->tui->component_count > 0) {
        tui_remove_component(state->tui, state->tui->components[0]);
    }

    /* Don't free the TUI if it belongs to pi */
    state->tui = NULL;

    interactive_state_free(state);

    ai_registry_cleanup();
    http_global_cleanup();

    return rc;
}
