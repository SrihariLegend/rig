#include "interactive.h"
#include "rig.h"
#include "harness/tools/tools.h"
#include "harness/model_registry.h"
#include "ai/providers/anthropic.h"
#include "ai/providers/openai.h"
#include "ai/providers/google.h"
#include "ai/providers/bedrock.h"
#include "ai/providers/mistral.h"
#include "util/log.h"
#include "harness/turnlog.h"
#include "harness/settings.h"
#include "harness/extensions/lua_ext.h"
#include "harness/permissions.h"
#include "harness/ext_build_prompt.h"
#include "ai/registry.h"
#include "tui/viewport.h"
#include "tui/linestore.h"
#include "tui/terminal.h"
#include "tui/keys.h"
#include "tui/ansi.h"
#include "tui/selector.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>

/* ---- State ---- */

typedef enum {
    ISTATE_IDLE,
    ISTATE_STREAMING,
} InteractivePhase;

typedef struct {
    RigInstance *rig;
    Session *session;
    AgentState *agent;
    AgentLoopConfig agent_config;

    LineStore *store;
    Viewport *viewport;

    /* Input buffer */
    char *input_buf;
    int input_len;
    int input_cap;
    int input_cursor;

    InteractivePhase phase;
    uint32_t msg_counter;
    bool thinking_active;
    char *last_prompt;
    int last_prompt_store_idx;

    const Model *model;
    int total_tokens;

    pthread_mutex_t mutex;
    volatile bool needs_render;
    volatile bool running;

    Tool *tools;
    int tool_count;
    int tool_cap;
    char cwd[4096];
    char *api_key;
    TurnLog *turnlog;
    PermissionSet *perms;
    volatile bool permission_pending;  /* pause main stdin poll */

    /* Input history */
    char **history;
    int history_count;
    int history_cap;
    int history_pos;        /* -1 = editing new input, 0..count-1 = browsing */
    char *history_stash;    /* saves in-progress input when browsing */
} InteractiveState;

/* ---- Forward Decls ---- */

static void on_agent_event(AgentEvent *event, void *userdata);
static void handle_submit(InteractiveState *state);
static void *agent_thread_fn(void *arg);
static void input_insert(InteractiveState *state, const char *text, int len);

/* ---- Input Handling ---- */

static void input_init(InteractiveState *state) {
    state->input_cap = 256;
    state->input_buf = calloc(state->input_cap, 1);
    state->input_len = 0;
    state->input_cursor = 0;
}

static void input_insert(InteractiveState *state, const char *text, int len) {
    while (state->input_len + len >= state->input_cap) {
        int new_cap = state->input_cap * 2;
        char *new_buf = realloc(state->input_buf, new_cap);
        if (!new_buf) return;
        state->input_buf = new_buf;
        state->input_cap = new_cap;
    }
    memmove(state->input_buf + state->input_cursor + len,
            state->input_buf + state->input_cursor,
            state->input_len - state->input_cursor);
    memcpy(state->input_buf + state->input_cursor, text, len);
    state->input_cursor += len;
    state->input_len += len;
    state->input_buf[state->input_len] = '\0';
}

static void input_backspace(InteractiveState *state) {
    if (state->input_cursor <= 0) return;
    memmove(state->input_buf + state->input_cursor - 1,
            state->input_buf + state->input_cursor,
            state->input_len - state->input_cursor);
    state->input_cursor--;
    state->input_len--;
    state->input_buf[state->input_len] = '\0';
}

static void input_clear(InteractiveState *state) {
    state->input_len = 0;
    state->input_cursor = 0;
    state->input_buf[0] = '\0';
}

static void sync_input_to_renderer(InteractiveState *state) {
    viewport_set_input(state->viewport, state->input_buf, state->input_cursor);
    state->needs_render = true;
}

/* ---- Input History ---- */

static void history_init(InteractiveState *state) {
    state->history_cap = 64;
    state->history = calloc(state->history_cap, sizeof(char *));
    state->history_count = 0;
    state->history_pos = -1;
    state->history_stash = NULL;
}

static void history_push(InteractiveState *state, const char *text) {
    if (!text || !text[0]) return;

    /* Skip duplicate of last entry */
    if (state->history_count > 0 &&
        strcmp(state->history[state->history_count - 1], text) == 0) return;

    if (state->history_count >= state->history_cap) {
        int new_cap = state->history_cap * 2;
        char **nh = realloc(state->history, (size_t)new_cap * sizeof(char *));
        if (!nh) return;
        state->history = nh;
        state->history_cap = new_cap;
    }

    state->history[state->history_count++] = strdup(text);
    state->history_pos = -1;
    free(state->history_stash);
    state->history_stash = NULL;
}

static void history_free(InteractiveState *state) {
    for (int i = 0; i < state->history_count; i++) {
        free(state->history[i]);
    }
    free(state->history);
    free(state->history_stash);
}

static void history_set_input(InteractiveState *state, const char *text) {
    input_clear(state);
    if (text && text[0]) {
        input_insert(state, text, (int)strlen(text));
    }
    sync_input_to_renderer(state);
    state->needs_render = true;
}

static void history_up(InteractiveState *state) {
    if (state->history_count == 0) return;

    if (state->history_pos == -1) {
        /* Stash current input */
        free(state->history_stash);
        state->history_stash = strdup(state->input_buf);
        state->history_pos = state->history_count - 1;
    } else if (state->history_pos > 0) {
        state->history_pos--;
    } else {
        return;
    }

    history_set_input(state, state->history[state->history_pos]);
}

static void history_down(InteractiveState *state) {
    if (state->history_pos == -1) return;

    if (state->history_pos < state->history_count - 1) {
        state->history_pos++;
        history_set_input(state, state->history[state->history_pos]);
    } else {
        /* Restore stashed input */
        state->history_pos = -1;
        history_set_input(state, state->history_stash);
        free(state->history_stash);
        state->history_stash = NULL;
    }
}

/* ---- Lua tool execution bridge ---- */

/* Thread-local: set by before_tool hook so execute() knows which tool it is */
static _Thread_local const char *lua_tool_current_name = NULL;

static int lua_tool_execute(const char *call_id, cJSON *params, void *signal,
                            void (*on_update)(void *ctx, cJSON *partial), void *ctx,
                            ContentBlock **content, int *content_count,
                            cJSON **details, bool *terminate) {
    (void)call_id; (void)signal; (void)on_update; (void)ctx; (void)details; (void)terminate;

    extern InteractiveState *g_interactive_state;
    InteractiveState *istate = g_interactive_state;
    if (!istate || !istate->rig || !istate->rig->api || !lua_tool_current_name) {
        *content = malloc(sizeof(ContentBlock));
        (*content)[0] = content_text("lua tool: no context", NULL);
        *content_count = 1;
        return -1;
    }

    RigExtensionAPI *eapi = istate->rig->api;
    char *result = NULL;
    int rc = -1;
    for (int i = 0; i < eapi->extension_count; i++) {
        Extension *ext = eapi->extensions[i];
        if (ext && ext->is_lua && ext->lua_state) {
            rc = lua_ext_call_tool((LuaExtState *)ext->lua_state,
                                   lua_tool_current_name, params, &result);
            if (rc == 0) break;
        }
    }

    *content = malloc(sizeof(ContentBlock));
    (*content)[0] = content_text(result ? result : "lua tool: no result", NULL);
    *content_count = 1;
    free(result);
    return rc == 0 ? 0 : -1;
}

/* ---- LLM call helper for /ext build ---- */

typedef struct {
    Str *text;
    volatile bool done;
} ExtBuildBridge;

static void ext_build_stream_cb(StreamEvent *event, void *ud) {
    ExtBuildBridge *b = (ExtBuildBridge *)ud;
    if (event->type == EVENT_TEXT_DELTA && event->delta) str_append(b->text, event->delta);
    if (event->type == EVENT_DONE) b->done = true;
    if (event->type == EVENT_ERROR && event->error_message) {
        str_append(b->text, "[error: ");
        str_append(b->text, event->error_message);
        str_append(b->text, "]");
        b->done = true;
    }
}

/* ---- Startup Splash ---- */

static int splash_vis_width(const char *s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { w++; p++; }
        else if (*p < 0xE0) { w++; p += 2; }
        else if (*p < 0xF0) { w++; p += 3; }
        else { w += 2; p += 4; }
    }
    return w;
}

static char *splash_center(const char *text, int term_width) {
    int vw = splash_vis_width(text);
    int pad = (term_width - vw) / 2;
    if (pad < 0) pad = 0;

    size_t tlen = strlen(text);
    char *buf = malloc(pad + tlen + 1);
    if (!buf) return strdup(text);
    memset(buf, ' ', pad);
    memcpy(buf + pad, text, tlen + 1);
    return buf;
}

static char *splash_center_layers(const char *layers, int text_vis_width, int term_width) {
    int pad = (term_width - text_vis_width) / 2;
    if (pad < 0) pad = 0;

    size_t llen = strlen(layers);
    char *buf = malloc(pad + llen + 1);
    if (!buf) return strdup(layers);
    memset(buf, '.', pad);
    memcpy(buf + pad, layers, llen + 1);
    return buf;
}

static void show_splash(InteractiveState *state) {
    typedef struct { const char *text; const char *layers; } SailLine;

    static const SailLine sail[] = {
        { "\xe2\x96\xb2",                                                                                                       "O" },
        { "\xe2\x95\xb1   \xe2\x95\xb2",                                                                                       "O...O" },
        { "\xe2\x95\xb1       \xe2\x95\xb2",                                                                                   "O.......O" },
        { "\xe2\x95\xb1    \xe2\x94\x82    \xe2\x95\xb2",                                                                       "O....m....O" },
        { "\xe2\x95\xb1    \xe2\x95\xb1 \xe2\x94\x82 \xe2\x95\xb2    \xe2\x95\xb2",                                               "O....M.m.M....O" },
        { "\xe2\x95\xb1  \xe2\x95\xb1    \xe2\x94\x82    \xe2\x95\xb2  \xe2\x95\xb2",                                               "O..M....m....M..O" },
        { "\xe2\x95\xb1      \xe2\x95\xb1 \xe2\x94\x82 \xe2\x95\xb2      \xe2\x95\xb2",                                           "O......I.m.I......O" },
        { "\xe2\x95\xb1 \xe2\x95\xb1   \xe2\x95\xb1   \xe2\x94\x82   \xe2\x95\xb2   \xe2\x95\xb2 \xe2\x95\xb2",                   "O.M...I...m...I...M.O" },
        { "\xe2\x95\xb1    \xe2\x95\xb1   \xe2\x95\xb1  \xe2\x94\x82  \xe2\x95\xb2   \xe2\x95\xb2    \xe2\x95\xb2",               "O....M...I..m..I...M....O" },
        { "\xe2\x95\xb1  \xe2\x95\xb1    \xe2\x95\xb1    \xe2\x94\x82    \xe2\x95\xb2    \xe2\x95\xb2  \xe2\x95\xb2",               "O..M....I....m....I....M..O" },
        { "\xe2\x95\xb1    \xe2\x95\xb1 \xe2\x95\xb1   \xe2\x95\xb1  \xe2\x94\x82  \xe2\x95\xb2   \xe2\x95\xb2 \xe2\x95\xb2    \xe2\x95\xb2", "O....M.I...I..m..I...I.M....O" },
        { "\xe2\x95\xb1 \xe2\x95\xb1   \xe2\x95\xb1  \xe2\x95\xb1   \xe2\x95\xb1 \xe2\x94\x82 \xe2\x95\xb2   \xe2\x95\xb2  \xe2\x95\xb2   \xe2\x95\xb2 \xe2\x95\xb2", "O.M...I..I...I.m.I...I..I...M.O" },
    };
    static const char *base_lines[] = {
        "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
        "\xe2\x94\x82",
        "\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80",
    };
    static const char *logo[] = {
        "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97  \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97",
        "\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d",
        "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x9d \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97",
        "\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91   \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91",
        "\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91 \xe2\x95\x9a\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x9d",
        "\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d",
    };

    int tw = state->viewport->term_width;

    linestore_add_blank(state->store);
    linestore_add_blank(state->store);

    /* Sail — layered per-char brightness */
    for (int i = 0; i < 12; i++) {
        char *centered = splash_center(sail[i].text, tw);
        char *clayers = splash_center_layers(sail[i].layers,
                            splash_vis_width(sail[i].text), tw);
        linestore_add_splash_layered(state->store, centered, clayers);
        free(centered);
        free(clayers);
    }

    /* Base lines — single brightness */
    for (int i = 0; i < 3; i++) {
        char *centered = splash_center(base_lines[i], tw);
        linestore_add_splash(state->store, centered, 0.50f);
        free(centered);
    }

    linestore_add_blank(state->store);
    linestore_add_blank(state->store);

    /* Logo */
    for (int i = 0; i < 6; i++) {
        char *centered = splash_center(logo[i], tw);
        linestore_add_splash(state->store, centered, 0.85f);
        free(centered);
    }

    linestore_add_blank(state->store);

    /* Info */
    if (state->model) {
        char info[256];
        snprintf(info, sizeof(info), "%s  \xc2\xb7  v%s", state->model->name, RIG_VERSION);
        char *centered = splash_center(info, tw);
        linestore_add_splash(state->store, centered, 0.35f);
        free(centered);
    } else {
        char info[128];
        snprintf(info, sizeof(info), "v%s", RIG_VERSION);
        char *centered = splash_center(info, tw);
        linestore_add_splash(state->store, centered, 0.35f);
        free(centered);
    }
    {
        char *centered = splash_center("/help for commands", tw);
        linestore_add_splash(state->store, centered, 0.35f);
        free(centered);
    }

    linestore_add_blank(state->store);
    linestore_add_blank(state->store);
}

/* ---- Session Restore ---- */

static int restore_session(InteractiveState *state) {
    if (!state->session || state->session->entry_count == 0) return 0;

    int restored = 0;
    for (int i = 0; i < state->session->entry_count; i++) {
        SessionEntry *e = &state->session->entries[i];
        if (e->type != ENTRY_MESSAGE || !e->data) continue;

        cJSON *role_j = cJSON_GetObjectItem(e->data, "role");
        cJSON *text_j = cJSON_GetObjectItem(e->data, "text");
        if (!role_j || !cJSON_IsString(role_j)) continue;
        if (!text_j || !cJSON_IsString(text_j)) continue;

        const char *role = role_j->valuestring;
        const char *text = text_j->valuestring;

        if (strcmp(role, "user") == 0) {
            Message *msg = message_create_user(text);
            if (msg) {
                agent_state_add_message(state->agent, msg);
                linestore_add_user_text(state->store, text);
                linestore_add_blank(state->store);
                history_push(state, text);
                restored++;
            }
        } else if (strcmp(role, "assistant") == 0) {
            Message *msg = message_create_assistant();
            if (msg) {
                message_add_content(msg, content_text(text, NULL));
                agent_state_add_message(state->agent, msg);
                state->msg_counter++;
                linestore_begin_message(state->store, state->msg_counter);
                linestore_append_assistant_text(state->store, text);
                linestore_flush_stream(state->store);
                linestore_add_blank(state->store);
                restored++;
            }
        }
    }
    if (restored > 0) {
        LOG_INFO("session restore: replayed %d messages", restored);
    }
    return restored;
}

/* ---- Agent Events ---- */

static void on_agent_event(AgentEvent *event, void *userdata) {
    InteractiveState *state = userdata;
    if (!state) return;

    switch (event->type) {
    case AGENT_EVENT_MESSAGE_START:
        if (event->message && event->message->role == ROLE_ASSISTANT) {
            pthread_mutex_lock(&state->mutex);
            state->msg_counter++;
            linestore_begin_message(state->store, state->msg_counter);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    case AGENT_EVENT_MESSAGE_UPDATE:
        if (!event->stream_event) break;
        LOG_DEBUG("stream event type=%d delta=%s", event->stream_event->type,
                  event->stream_event->delta ? "yes" : "no");
        switch (event->stream_event->type) {
        case EVENT_TEXT_DELTA:
            if (event->stream_event->delta) {
                pthread_mutex_lock(&state->mutex);
                linestore_append_assistant_text(state->store, event->stream_event->delta);
                if (state->viewport->auto_scroll) {
                    int total = linestore_screen_row_count(state->store);
                    int vp = state->viewport->term_height - 1;
                    state->viewport->scroll_offset = total - vp;
                    if (state->viewport->scroll_offset < 0)
                        state->viewport->scroll_offset = 0;
                }
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
            }
            break;
        case EVENT_THINKING_START:
            state->thinking_active = true;
            break;
        case EVENT_THINKING_END:
            state->thinking_active = false;
            break;
        case EVENT_TOOLCALL_START:
            break;
        case EVENT_TOOLCALL_END:
            break;
        case EVENT_DONE:
            state->phase = ISTATE_IDLE;
            state->thinking_active = false;
            if (event->stream_event->message) {
                state->total_tokens += event->stream_event->message->usage.total_tokens;
            }
            pthread_mutex_lock(&state->mutex);
            viewport_set_breathing(state->viewport, false, NULL);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
            break;
        case EVENT_ERROR:
            state->phase = ISTATE_IDLE;
            if (event->stream_event->error_message) {
                pthread_mutex_lock(&state->mutex);
                linestore_add_error(state->store, event->stream_event->error_message);
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
            }
            break;
        default:
            break;
        }
        break;

    case AGENT_EVENT_MESSAGE_END:
        if (event->message && event->message->role == ROLE_ASSISTANT) {
            pthread_mutex_lock(&state->mutex);
            linestore_flush_stream(state->store);
            state->viewport->auto_scroll = true;
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
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
                cJSON_Delete(msg_data);
                str_free(&full_text);
            }
            pthread_mutex_lock(&state->mutex);
            linestore_add_blank(state->store);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        } else if (event->message && event->message->role == ROLE_TOOL_RESULT) {
            pthread_mutex_lock(&state->mutex);
            for (int i = 0; i < event->message->content_count; i++) {
                if (event->message->content[i].type == CONTENT_TEXT &&
                    event->message->content[i].text.text) {
                    linestore_add_tool_output(state->store, event->message->content[i].text.text);
                }
            }
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    case AGENT_EVENT_TOOL_EXEC_START:
        if (event->tool_name) {
            pthread_mutex_lock(&state->mutex);
            char *args_str = event->args ? cJSON_PrintUnformatted(event->args) : NULL;
            if (args_str && strlen(args_str) > 200) {
                args_str[197] = '.';
                args_str[198] = '.';
                args_str[199] = '.';
                args_str[200] = '\0';
            }
            linestore_add_tool_start(state->store, event->tool_name, args_str);
            free(args_str);
            viewport_set_breathing(state->viewport, true, event->tool_name);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    case AGENT_EVENT_TOOL_EXEC_END:
        if (event->tool_name) {
            pthread_mutex_lock(&state->mutex);
            linestore_add_tool_done(state->store, event->tool_name);
            viewport_set_breathing(state->viewport, false, NULL);
            if (state->turnlog && turnlog_budget_exceeded(state->turnlog) && !state->turnlog->budget_warned) {
                state->turnlog->budget_warned = true;
                linestore_add_system(state->store, "snapshot budget exceeded — file undo disabled for remaining turns");
            }
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    default:
        break;
    }
}

/* ---- Submit ---- */

/* Extract a human-readable argument summary for permission display + pattern matching */
static char *tool_arg_summary(const char *name, cJSON *args) {
    if (!args) return NULL;

    if (strcmp(name, "bash") == 0) {
        cJSON *cmd = cJSON_GetObjectItem(args, "command");
        return (cmd && cJSON_IsString(cmd)) ? strdup(cmd->valuestring) : NULL;
    }
    if (strcmp(name, "read") == 0 || strcmp(name, "write") == 0 || strcmp(name, "edit") == 0) {
        cJSON *fp = cJSON_GetObjectItem(args, "file_path");
        return (fp && cJSON_IsString(fp)) ? strdup(fp->valuestring) : NULL;
    }
    if (strcmp(name, "grep") == 0) {
        cJSON *pat = cJSON_GetObjectItem(args, "pattern");
        return (pat && cJSON_IsString(pat)) ? strdup(pat->valuestring) : NULL;
    }
    /* Extension tools: stringify first string arg */
    cJSON *child = args->child;
    while (child) {
        if (cJSON_IsString(child)) return strdup(child->valuestring);
        child = child->next;
    }
    return NULL;
}

static int before_tool(BeforeToolCallContext *ctx, BeforeToolCallResult *result) {
    extern InteractiveState *g_interactive_state;
    InteractiveState *state = g_interactive_state;
    if (!state) return 0;

    const char *name = ctx->tool_call ? ctx->tool_call->tool_call.name : NULL;
    if (!name) return 0;

    /* Set thread-local for Lua tool bridge */
    lua_tool_current_name = name;

    /* Snapshot files before write/edit */
    if (state->turnlog) {
        if (strcmp(name, "write") == 0 || strcmp(name, "edit") == 0) {
            cJSON *fp = ctx->args ? cJSON_GetObjectItem(ctx->args, "file_path") : NULL;
            if (fp && cJSON_IsString(fp)) {
                turnlog_snapshot_file(state->turnlog, fp->valuestring);
            }
        }
    }

    /* Permission check */
    if (state->perms) {
        char *summary = tool_arg_summary(name, ctx->args);
        bool trusted = permissions_check(state->perms, name, summary);

        if (!trusted) {
            /* Show tool call description */
            char *desc = permissions_describe_call(name, summary);

            pthread_mutex_lock(&state->mutex);
            linestore_add_system(state->store, desc);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);

            free(desc);

            /* Build permission options for selector */
            char pattern_label[256];
            if (summary && strcmp(name, "bash") == 0) {
                char *space = strchr(summary, ' ');
                if (space) {
                    int prefix_len = (int)(space - summary);
                    snprintf(pattern_label, sizeof(pattern_label),
                             "Always allow: %s '%.*s *'", name, prefix_len, summary);
                } else {
                    snprintf(pattern_label, sizeof(pattern_label),
                             "Always allow: %s '%s'", name, summary);
                }
            } else if (summary) {
                snprintf(pattern_label, sizeof(pattern_label),
                         "Always allow: %s '%s'", name, summary);
            } else {
                snprintf(pattern_label, sizeof(pattern_label),
                         "Always allow: %s (this pattern)", name);
            }

            char tool_label[128];
            snprintf(tool_label, sizeof(tool_label), "Always allow: %s (all)", name);

            const char *options[] = {
                "Allow",
                pattern_label,
                tool_label,
                "Deny",
            };

            state->permission_pending = true;
            int chosen = tui_selector(options, 4, 0);
            state->permission_pending = false;
            viewport_render_full(state->viewport);

            switch (chosen) {
            case 0: /* Allow once */
                break;
            case 1: /* Always allow pattern */
                if (summary && strcmp(name, "bash") == 0) {
                    char *space = strchr(summary, ' ');
                    if (space) {
                        size_t prefix_len = (size_t)(space - summary);
                        char *pattern = malloc(prefix_len + 3);
                        memcpy(pattern, summary, prefix_len);
                        pattern[prefix_len] = ' ';
                        pattern[prefix_len + 1] = '*';
                        pattern[prefix_len + 2] = '\0';
                        permissions_trust(state->perms, name, pattern);
                        free(pattern);
                    } else {
                        permissions_trust(state->perms, name, summary);
                    }
                } else if (summary) {
                    permissions_trust(state->perms, name, summary);
                } else {
                    permissions_trust(state->perms, name, NULL);
                }
                break;
            case 2: /* Always allow tool */
                permissions_trust(state->perms, name, NULL);
                break;
            default: /* Deny (chosen == 3 or -1/cancelled) */
                free(summary);
                result->block = true;
                result->reason = strdup("denied by user");
                pthread_mutex_lock(&state->mutex);
                linestore_add_system(state->store, "denied");
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
                return 0;
            }

            free(summary);
            pthread_mutex_lock(&state->mutex);
            linestore_add_system(state->store, "allowed");
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        } else {
            free(summary);
        }
    }

    return 0;
}

InteractiveState *g_interactive_state = NULL;

static void handle_submit(InteractiveState *state) {
    if (state->input_len == 0) return;

    history_push(state, state->input_buf);

    if (!state->model || !state->api_key) {
        pthread_mutex_lock(&state->mutex);
        linestore_add_error(state->store, "no API key configured");
        state->needs_render = true;
        pthread_mutex_unlock(&state->mutex);
        input_clear(state);
        sync_input_to_renderer(state);
        return;
    }

    char *prompt_text = strdup(state->input_buf);
    free(state->last_prompt);
    state->last_prompt = strdup(state->input_buf);
    input_clear(state);
    sync_input_to_renderer(state);

    pthread_mutex_lock(&state->mutex);
    state->msg_counter++;
    state->last_prompt_store_idx = state->store->count;
    linestore_add_user_text(state->store, prompt_text);
    linestore_add_blank(state->store);
    state->needs_render = true;
    pthread_mutex_unlock(&state->mutex);

    if (state->session) {
        cJSON *msg_data = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_data, "role", "user");
        cJSON_AddStringToObject(msg_data, "text", prompt_text);
        session_append(state->session, ENTRY_MESSAGE, msg_data);
        session_flush(state->session);
        cJSON_Delete(msg_data);
    }

    state->phase = ISTATE_STREAMING;
    state->viewport->is_streaming = true;

    /* Begin a new turn for undo tracking */
    turnlog_begin_turn(state->turnlog,
                       state->agent->message_count,
                       state->store->count);
    g_interactive_state = state;

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

    LOG_INFO("Agent thread: prompt='%.100s'", prompt_text);

    Message *prompt_msg = message_create_user(prompt_text);
    int rc = agent_prompt(state->agent, &prompt_msg, 1, &state->agent_config,
                          on_agent_event, state);

    LOG_INFO("Agent thread: agent_prompt returned %d", rc);

    state->phase = ISTATE_IDLE;
    pthread_mutex_lock(&state->mutex);
    state->viewport->is_streaming = false;
    state->needs_render = true;
    pthread_mutex_unlock(&state->mutex);

    free(prompt_text);
    return NULL;
}

/* ---- Helpers ---- */

static void collect_output(const char *data, size_t len, void *ctx) {
    str_append_len((Str *)ctx, data, len);
}

/* ---- Slash Commands ---- */

static void cmd_output(InteractiveState *state, const char *text) {
    linestore_add_system(state->store, text);
}

static void cmd_finish(InteractiveState *state) {
    state->needs_render = true;
    pthread_mutex_unlock(&state->mutex);
    input_clear(state);
    sync_input_to_renderer(state);
}

static bool handle_slash_command(InteractiveState *state) {
    const char *full = state->input_buf + 1;
    char cmd[64] = {0};
    const char *arg = NULL;

    const char *space = strchr(full, ' ');
    if (space) {
        int len = (int)(space - full);
        if (len > 63) len = 63;
        memcpy(cmd, full, len);
        arg = space + 1;
        while (*arg == ' ') arg++;
        if (!*arg) arg = NULL;
    } else {
        strncpy(cmd, full, 63);
    }

    /* /exit /quit /q */
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        state->running = false;
        return true;
    }

    /* /clear — reset context, keep display */
    if (strcmp(cmd, "clear") == 0) {
        pthread_mutex_lock(&state->mutex);
        agent_state_reset(state->agent);
        state->total_tokens = 0;
        cmd_output(state, "context cleared");
        cmd_finish(state);
        return true;
    }

    /* /model [name] or /model list */
    if (strcmp(cmd, "model") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (!arg) {
            const char *provider = auth_get_active_provider();
            int count = 0;
            const Model **all = models_get_all(provider, &count);
            if (count > 0) {
                const char **names = malloc((size_t)count * sizeof(char *));
                int current = 0;
                for (int i = 0; i < count; i++) {
                    names[i] = all[i]->name;
                    if (state->model && strcmp(state->model->id, all[i]->id) == 0)
                        current = i;
                }
                pthread_mutex_unlock(&state->mutex);
                int chosen = tui_selector(names, count, current);
                free(names);
                pthread_mutex_lock(&state->mutex);
                if (chosen >= 0 && chosen < count) {
                    const Model *found = all[chosen];
                    char *key = auth_get_api_key(found->provider);
                    if (key) {
                        state->model = found;
                        free(state->api_key);
                        state->api_key = key;
                        state->agent->model = found;
                        state->agent_config.model = found;
                        state->agent_config.max_tokens = found->max_tokens;
                        state->agent_config.api_key = key;
                        free(state->agent->system_prompt);
                        state->agent->system_prompt = system_prompt_build(
                            state->tools, state->tool_count, state->cwd);
                        char buf[256];
                        snprintf(buf, sizeof(buf), "switched to %s", found->name);
                        cmd_output(state, buf);
                    }
                }
            } else {
                cmd_output(state, "no models available");
            }
        } else if (strcmp(arg, "list") == 0) {
            const char *provider = auth_get_active_provider();
            int count = 0;
            const Model **all = models_get_all(provider, &count);
            for (int i = 0; i < count; i++) {
                char buf[256];
                const char *marker = (state->model && strcmp(state->model->id, all[i]->id) == 0) ? " *" : "";
                snprintf(buf, sizeof(buf), "  %s (%s)%s", all[i]->name, all[i]->id, marker);
                cmd_output(state, buf);
            }
        } else {
            const char *provider = auth_get_active_provider();
            int count = 0;
            const Model **all = models_get_all(provider, &count);
            const Model *found = NULL;
            for (int i = 0; i < count; i++) {
                if (strstr(all[i]->id, arg) || strstr(all[i]->name, arg)) {
                    found = all[i];
                    break;
                }
            }
            if (found) {
                char *key = auth_get_api_key(found->provider);
                if (key) {
                    state->model = found;
                    free(state->api_key);
                    state->api_key = key;
                    state->agent->model = found;
                    state->agent_config.model = found;
                    state->agent_config.max_tokens = found->max_tokens;
                    state->agent_config.api_key = key;
                    free(state->agent->system_prompt);
                    state->agent->system_prompt = system_prompt_build(
                        state->tools, state->tool_count, state->cwd);
                    char buf[256];
                    snprintf(buf, sizeof(buf), "switched to %s", found->name);
                    cmd_output(state, buf);
                } else {
                    cmd_output(state, "no API key for that model's provider");
                }
            } else {
                cmd_output(state, "model not found — try /model list");
            }
        }
        cmd_finish(state);
        return true;
    }

    /* /tools */
    if (strcmp(cmd, "tools") == 0) {
        pthread_mutex_lock(&state->mutex);
        for (int i = 0; i < state->tool_count; i++) {
            char buf[256];
            snprintf(buf, sizeof(buf), "  %s — %s",
                     state->tools[i].name,
                     state->tools[i].description ? state->tools[i].description : "");
            cmd_output(state, buf);
        }
        cmd_finish(state);
        return true;
    }

    /* /run <command> */
    if (strcmp(cmd, "run") == 0) {
        if (!arg) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "usage: /run <command>");
            cmd_finish(state);
            return true;
        }
        pthread_mutex_lock(&state->mutex);
        char label[256];
        snprintf(label, sizeof(label), "$ %s", arg);
        cmd_output(state, label);
        pthread_mutex_unlock(&state->mutex);

        Str output = str_new(4096);
        ProcessOptions opts = {
            .command = arg,
            .cwd = state->cwd,
            .timeout_ms = 30000,
            .on_stdout = collect_output,
            .on_stderr = collect_output,
            .ctx = &output,
        };
        ProcessResult result;
        process_run(&opts, &result);

        pthread_mutex_lock(&state->mutex);
        if (output.len > 0) {
            linestore_add_tool_output(state->store, output.data);
        }
        char rc_buf[64];
        snprintf(rc_buf, sizeof(rc_buf), "exit %d", result.exit_code);
        cmd_output(state, rc_buf);
        str_free(&output);
        cmd_finish(state);
        return true;
    }

    /* /undo */
    if (strcmp(cmd, "undo") == 0) {
        Turn *turn = turnlog_latest(state->turnlog);
        if (!turn) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "nothing to undo");
            cmd_finish(state);
            return true;
        }

        /* Show what will be undone */
        pthread_mutex_lock(&state->mutex);
        char buf[256];
        snprintf(buf, sizeof(buf), "undo turn %d:", turn->turn_id);
        cmd_output(state, buf);

        if (turn->snapshot_count > 0) {
            int delete_count = 0;
            for (int i = 0; i < turn->snapshot_count; i++) {
                bool is_delete = turn->snapshots[i].was_created;
                if (is_delete) delete_count++;
                snprintf(buf, sizeof(buf), "  %s %s",
                         is_delete ? "delete" : "revert",
                         turn->snapshots[i].file_path);
                cmd_output(state, buf);
            }

            /* Only require confirmation when files will be deleted */
            if (delete_count > 0) {
                snprintf(buf, sizeof(buf), "%d file%s will be deleted. confirm? y/n",
                         delete_count, delete_count == 1 ? "" : "s");
                cmd_output(state, buf);
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);

                struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
                bool confirmed = false;
                while (1) {
                    int ready = poll(&pfd, 1, 100);
                    if (ready > 0) {
                        char ch[16];
                        ssize_t n = read(STDIN_FILENO, ch, sizeof(ch) - 1);
                        if (n > 0) {
                            if (ch[0] == 'y' || ch[0] == 'Y') { confirmed = true; break; }
                            if (ch[0] == 'n' || ch[0] == 'N' || ch[0] == '\x1b' || ch[0] == '\x03') break;
                        }
                    }
                }

                pthread_mutex_lock(&state->mutex);
                if (!confirmed) {
                    cmd_output(state, "cancelled");
                    cmd_finish(state);
                    return true;
                }
            }

            int restored = turnlog_restore_turn(state->turnlog, turn);
            snprintf(buf, sizeof(buf), "reverted %d file%s", restored, restored == 1 ? "" : "s");
            cmd_output(state, buf);
        }

        /* Remove messages from agent context */
        if (state->agent->message_count > turn->msg_start_index) {
            state->agent->message_count = turn->msg_start_index;
        }

        /* Remove display lines */
        while (state->store->count > turn->store_start_index) {
            state->store->count--;
            state->store->total_screen_rows -= state->store->lines[state->store->count].wrap_count;
            free(state->store->lines[state->store->count].raw_text);
            free(state->store->lines[state->store->count].spans);
            state->store->lines[state->store->count].raw_text = NULL;
            state->store->lines[state->store->count].spans = NULL;
        }

        turnlog_pop(state->turnlog);
        cmd_output(state, "undone");
        cmd_finish(state);
        return true;
    }

    /* /diff */
    if (strcmp(cmd, "diff") == 0) {
        pthread_mutex_lock(&state->mutex);
        cmd_output(state, "$ git diff --stat");
        pthread_mutex_unlock(&state->mutex);

        Str output = str_new(4096);
        ProcessOptions opts = {
            .command = "git diff --stat",
            .cwd = state->cwd,
            .timeout_ms = 10000,
            .on_stdout = collect_output,
            .on_stderr = collect_output,
            .ctx = &output,
        };
        ProcessResult result;
        process_run(&opts, &result);

        pthread_mutex_lock(&state->mutex);
        if (output.len > 0) {
            linestore_add_tool_output(state->store, output.data);
        } else {
            cmd_output(state, "no changes");
        }
        (void)result;
        str_free(&output);
        cmd_finish(state);
        return true;
    }

    /* /find <query> */
    if (strcmp(cmd, "find") == 0) {
        if (!arg) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "usage: /find <query>");
            cmd_finish(state);
            return true;
        }
        /* Send as a prompt to the agent with search instruction */
        char *prompt = malloc(strlen(arg) + 128);
        if (prompt) {
            snprintf(prompt, strlen(arg) + 128,
                "Search the codebase for: %s\nUse grep and read tools. "
                "Report relevant files and line numbers. Be concise.", arg);
            input_clear(state);
            /* Insert as prompt text and submit */
            input_insert(state, prompt, (int)strlen(prompt));
            free(prompt);
            handle_submit(state);
        }
        return true;
    }

    /* /context */
    if (strcmp(cmd, "context") == 0) {
        pthread_mutex_lock(&state->mutex);
        char buf[256];
        snprintf(buf, sizeof(buf), "messages: %d", state->agent->message_count);
        cmd_output(state, buf);
        snprintf(buf, sizeof(buf), "tokens used: %d", state->total_tokens);
        cmd_output(state, buf);
        int ctx = state->model ? state->model->context_window : 0;
        if (ctx > 0) {
            snprintf(buf, sizeof(buf), "context window: %d", ctx);
            cmd_output(state, buf);
        }
        snprintf(buf, sizeof(buf), "tools: %d", state->tool_count);
        cmd_output(state, buf);
        snprintf(buf, sizeof(buf), "display lines: %d", state->store->count);
        cmd_output(state, buf);
        cmd_finish(state);
        return true;
    }

    /* /trust [list|reset|yolo|<tool> [pattern]] */
    if (strcmp(cmd, "trust") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (!arg || strcmp(arg, "list") == 0) {
            if (state->perms->yolo) {
                cmd_output(state, "mode: TRUST ALL (yolo)");
            } else if (state->perms->count == 0) {
                cmd_output(state, "no trust rules — all tools will prompt");
            } else {
                char buf[256];
                for (int i = 0; i < state->perms->count; i++) {
                    TrustRule *r = &state->perms->rules[i];
                    if (r->pattern) {
                        snprintf(buf, sizeof(buf), "  %s '%s'", r->tool, r->pattern);
                    } else {
                        snprintf(buf, sizeof(buf), "  %s (all)", r->tool);
                    }
                    cmd_output(state, buf);
                }
            }
        } else if (strcmp(arg, "reset") == 0) {
            permissions_free(state->perms);
            state->perms = permissions_create();
            permissions_trust(state->perms, "read", NULL);
            permissions_trust(state->perms, "grep", NULL);
            cmd_output(state, "trust reset to defaults (read/grep)");
        } else if (strcmp(arg, "yolo") == 0) {
            permissions_trust(state->perms, "*", NULL);
            cmd_output(state, "TRUST ALL — no more permission prompts");
        } else {
            /* /trust <tool> [pattern] */
            char tool[64] = {0};
            const char *pattern = NULL;
            const char *sp2 = strchr(arg, ' ');
            if (sp2) {
                int tlen = (int)(sp2 - arg);
                if (tlen > 63) tlen = 63;
                memcpy(tool, arg, tlen);
                pattern = sp2 + 1;
                while (*pattern == ' ') pattern++;
                if (!*pattern) pattern = NULL;
            } else {
                strncpy(tool, arg, 63);
            }
            permissions_trust(state->perms, tool, pattern);
            char buf[256];
            if (pattern) {
                snprintf(buf, sizeof(buf), "trusted: %s '%s'", tool, pattern);
            } else {
                snprintf(buf, sizeof(buf), "trusted: %s (all)", tool);
            }
            cmd_output(state, buf);
        }
        cmd_finish(state);
        return true;
    }

    /* /session <id> — switch to a session */
    if (strcmp(cmd, "session") == 0) {
        if (!arg) {
            pthread_mutex_lock(&state->mutex);
            if (state->session) {
                char buf[256];
                snprintf(buf, sizeof(buf), "current session: %s",
                         state->session->session_id ? state->session->session_id : "none");
                cmd_output(state, buf);
            }
            cmd_output(state, "usage: /session <id>");
            cmd_finish(state);
            return true;
        }

        const char *dir = config_sessions_dir();
        if (!dir) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "no sessions directory");
            cmd_finish(state);
            return true;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s.jsonl", dir, arg);
        if (!fs_exists(path)) {
            pthread_mutex_lock(&state->mutex);
            char buf[256];
            snprintf(buf, sizeof(buf), "session not found: %s", arg);
            cmd_output(state, buf);
            cmd_finish(state);
            return true;
        }

        Session *new_session = session_load(path);
        if (!new_session) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "failed to load session");
            cmd_finish(state);
            return true;
        }

        pthread_mutex_lock(&state->mutex);

        /* Clear agent context */
        agent_state_reset(state->agent);
        state->total_tokens = 0;

        /* Clear linestore */
        linestore_clear(state->store);

        /* Swap session */
        session_free(state->session);
        state->session = new_session;

        /* Replay messages */
        int restored = restore_session(state);

        char buf[256];
        snprintf(buf, sizeof(buf), "loaded session %s (%d messages)",
                 new_session->session_id, restored);
        cmd_output(state, buf);

        /* Reset scroll to bottom */
        state->viewport->auto_scroll = true;
        int total = linestore_screen_row_count(state->store);
        int vp = state->viewport->term_height - 1;
        state->viewport->scroll_offset = total > vp ? total - vp : 0;

        cmd_finish(state);
        return true;
    }

    /* /sessions — interactive picker */
    if (strcmp(cmd, "sessions") == 0) {
        const char *dir = config_sessions_dir();
        if (!dir) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "no sessions directory");
            cmd_finish(state);
            return true;
        }

        /* Collect session files sorted by modification time */
        char list_cmd[512];
        snprintf(list_cmd, sizeof(list_cmd), "ls -1t '%s'/*.jsonl 2>/dev/null | head -30", dir);
        Str output = str_new(2048);
        ProcessOptions popts = {
            .command = list_cmd,
            .timeout_ms = 5000,
            .on_stdout = collect_output,
            .ctx = &output,
        };
        ProcessResult presult;
        process_run(&popts, &presult);

        if (!output.data || output.len == 0) {
            str_free(&output);
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "no sessions found");
            cmd_finish(state);
            return true;
        }

        /* Parse file list into array */
        char *paths[30] = {0};
        char *labels[30] = {0};
        int count = 0;

        char *line = output.data;
        while (line && *line && count < 30) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (strlen(line) > 0) {
                paths[count] = strdup(line);

                /* Build display label from filename */
                const char *fname = strrchr(line, '/');
                fname = fname ? fname + 1 : line;
                /* Strip .jsonl */
                char label[128];
                strncpy(label, fname, sizeof(label) - 1);
                char *ext = strstr(label, ".jsonl");
                if (ext) *ext = '\0';

                /* Replace dashes with spaces for readability, mark current */
                bool is_current = state->session && state->session->file_path &&
                                  strcmp(state->session->file_path, line) == 0;
                if (is_current) {
                    char marked[140];
                    snprintf(marked, sizeof(marked), "%s  *", label);
                    labels[count] = strdup(marked);
                } else {
                    labels[count] = strdup(label);
                }
                count++;
            }
            line = nl ? nl + 1 : NULL;
        }

        if (count == 0) {
            str_free(&output);
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "no sessions found");
            cmd_finish(state);
            return true;
        }

        int selected = tui_selector((const char **)labels, count, 0);

        if (selected >= 0 && paths[selected]) {
            /* Load selected session */
            Session *new_session = session_load(paths[selected]);
            if (new_session) {
                pthread_mutex_lock(&state->mutex);
                agent_state_reset(state->agent);
                state->total_tokens = 0;
                linestore_clear(state->store);
                session_free(state->session);
                state->session = new_session;
                int restored = restore_session(state);

                char lbuf[256];
                snprintf(lbuf, sizeof(lbuf), "loaded %s (%d messages)",
                         new_session->keyword ? new_session->keyword : new_session->session_id,
                         restored);
                cmd_output(state, lbuf);
                state->viewport->auto_scroll = true;
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
            } else {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "failed to load session");
                pthread_mutex_unlock(&state->mutex);
            }
        }

        /* Cleanup */
        for (int i = 0; i < count; i++) { free(paths[i]); free(labels[i]); }
        str_free(&output);
        input_clear(state);
        sync_input_to_renderer(state);
        return true;
    }

    /* /fork */
    if (strcmp(cmd, "fork") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (state->session) {
            const char *dir = config_sessions_dir();
            Session *new_session = session_create(dir);
            if (new_session) {
                char buf[256];
                snprintf(buf, sizeof(buf), "forked to session %s", new_session->session_id);
                cmd_output(state, buf);
                session_free(state->session);
                state->session = new_session;
            } else {
                cmd_output(state, "failed to create new session");
            }
        }
        cmd_finish(state);
        return true;
    }

    /* /theme — stubbed, will be re-added on top of viewport */
    if (strcmp(cmd, "theme") == 0) {
        pthread_mutex_lock(&state->mutex);
        cmd_output(state, "themes not yet wired to viewport — coming soon");
        cmd_finish(state);
        return true;
    }

    /* /help [command] */
    if (strcmp(cmd, "help") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (!arg) {
            cmd_output(state, "/help [cmd]  — show help");
            cmd_output(state, "/model [name|list] — show/switch model");
            cmd_output(state, "/tools       — list available tools");
            cmd_output(state, "/run <cmd>   — run shell command directly");
            cmd_output(state, "/find <query> — search codebase via agent");
            cmd_output(state, "/diff        — git diff --stat");
            cmd_output(state, "/undo        — remove last exchange");
            cmd_output(state, "/context     — show context window usage");
            cmd_output(state, "/session <id> — load a saved session");
            cmd_output(state, "/sessions    — list saved sessions");
            cmd_output(state, "/fork        — fork to new session");
            cmd_output(state, "/theme <name> — switch color theme");
            cmd_output(state, "/trust [args] — manage tool permissions");
            cmd_output(state, "/ext <sub>    — extension manager");
            cmd_output(state, "/clear       — clear conversation");
            cmd_output(state, "/exit        — quit (/q)");
        } else if (strcmp(arg, "model") == 0) {
            cmd_output(state, "/model         — show current model and token count");
            cmd_output(state, "/model list    — list all available models for your provider");
            cmd_output(state, "/model <name>  — switch to model matching name (partial match)");
        } else if (strcmp(arg, "theme") == 0) {
            cmd_output(state, "/theme         — list available themes");
            cmd_output(state, "/theme <name>  — switch theme");
            cmd_output(state, "  default  — warm amber on dark");
            cmd_output(state, "  midnight — cool blue");
            cmd_output(state, "  ember    — hot orange");
            cmd_output(state, "  ghost    — pale blue, deep fade");
            cmd_output(state, "  daylight — dark text (for light terminals)");
        } else if (strcmp(arg, "find") == 0) {
            cmd_output(state, "/find <query>  — semantic search via agent");
            cmd_output(state, "sends query to the model with instruction to grep/read");
        } else if (strcmp(arg, "run") == 0) {
            cmd_output(state, "/run <cmd>     — run shell command, show output");
            cmd_output(state, "bypasses the agent, runs directly");
        } else if (strcmp(arg, "diff") == 0) {
            cmd_output(state, "/diff          — run git diff --stat in working directory");
        } else if (strcmp(arg, "undo") == 0) {
            cmd_output(state, "/undo          — remove last user message + assistant response");
            cmd_output(state, "also removes from agent context");
        } else if (strcmp(arg, "context") == 0) {
            cmd_output(state, "/context       — show context window state");
            cmd_output(state, "messages in context, tokens used, window size, tool count");
        } else if (strcmp(arg, "session") == 0) {
            cmd_output(state, "/session       — show current session ID");
            cmd_output(state, "/session <id>  — switch to saved session (clears context, replays history)");
        } else if (strcmp(arg, "sessions") == 0) {
            cmd_output(state, "/sessions      — list recent session files");
            cmd_output(state, "shows up to 20 most recent sessions");
        } else if (strcmp(arg, "fork") == 0) {
            cmd_output(state, "/fork          — create new session, keep conversation on screen");
            cmd_output(state, "context carries over, new session ID for persistence");
        } else if (strcmp(arg, "trust") == 0) {
            cmd_output(state, "/trust             — show current trust rules");
            cmd_output(state, "/trust list        — same as above");
            cmd_output(state, "/trust reset       — reset to defaults (read/grep/ls trusted)");
            cmd_output(state, "/trust yolo        — trust ALL tools, no prompts");
            cmd_output(state, "/trust bash        — trust all bash commands");
            cmd_output(state, "/trust bash 'git *' — trust bash commands starting with git");
            cmd_output(state, "/trust write /home/* — trust writes under /home/");
            cmd_output(state, "during prompts: [y]es [n]o [t]rust tool [a]llow pattern [!] yolo");
        } else if (strcmp(arg, "ext") == 0) {
            cmd_output(state, "/ext add <url>     — install from URL or git repo");
            cmd_output(state, "/ext list          — show loaded extensions");
            cmd_output(state, "/ext remove <name> — delete extension");
            cmd_output(state, "/ext reload        — hot-reload all extensions");
            cmd_output(state, "/ext edit <name>   — open in $EDITOR");
        } else if (strcmp(arg, "clear") == 0) {
            cmd_output(state, "/clear         — reset agent context (model forgets conversation)");
            cmd_output(state, "display stays visible, only the model's memory is wiped");
        } else if (strcmp(arg, "tools") == 0) {
            cmd_output(state, "/tools         — list all tools available to the model");
        } else if (strcmp(arg, "exit") == 0 || strcmp(arg, "q") == 0) {
            cmd_output(state, "/exit or /q    — quit rig");
        } else if (strcmp(arg, "help") == 0) {
            cmd_output(state, "/help          — list all commands");
            cmd_output(state, "/help <cmd>    — detailed help for a command");
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "no help for '%s'", arg);
            cmd_output(state, buf);
        }
        cmd_finish(state);
        return true;
    }

    /* /ext [build|add|list|remove|reload|edit] */
    if (strcmp(cmd, "ext") == 0) {
        if (!arg) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "usage: /ext [add|list|remove|reload|edit]");
            cmd_finish(state);
            return true;
        }

        /* Parse subcommand */
        char subcmd[32] = {0};
        const char *subarg = NULL;
        const char *sp = strchr(arg, ' ');
        if (sp) {
            int slen = (int)(sp - arg);
            if (slen > 31) slen = 31;
            memcpy(subcmd, arg, slen);
            subarg = sp + 1;
            while (*subarg == ' ') subarg++;
            if (!*subarg) subarg = NULL;
        } else {
            strncpy(subcmd, arg, 31);
        }

        /* /ext list */
        if (strcmp(subcmd, "list") == 0) {
            pthread_mutex_lock(&state->mutex);
            if (state->rig && state->rig->api) {
                RigExtensionAPI *api = state->rig->api;
                if (api->extension_count == 0) {
                    cmd_output(state, "no extensions loaded");
                } else {
                    char lbuf[256];
                    for (int i = 0; i < api->extension_count; i++) {
                        Extension *ext = api->extensions[i];
                        snprintf(lbuf, sizeof(lbuf), "  %s%s%s",
                                 ext->name ? ext->name : "?",
                                 ext->is_lua ? " (lua)" : ext->is_yaml ? " (yaml)" : " (native)",
                                 ext->path ? "" : " [no path]");
                        cmd_output(state, lbuf);
                    }
                }
            } else {
                cmd_output(state, "no extension API");
            }
            cmd_finish(state);
            return true;
        }

        /* /ext remove <name> */
        if (strcmp(subcmd, "remove") == 0) {
            if (!subarg) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "usage: /ext remove <name>");
                cmd_finish(state);
                return true;
            }
            /* Find extension by name and delete file */
            pthread_mutex_lock(&state->mutex);
            bool found = false;
            if (state->rig && state->rig->api) {
                for (int i = 0; i < state->rig->api->extension_count; i++) {
                    Extension *ext = state->rig->api->extensions[i];
                    if (ext && ext->name && strstr(ext->name, subarg)) {
                        if (ext->path && fs_exists(ext->path)) {
                            unlink(ext->path);
                            char lbuf[256];
                            snprintf(lbuf, sizeof(lbuf), "removed %s", ext->name);
                            cmd_output(state, lbuf);
                            found = true;
                        }
                        break;
                    }
                }
            }
            if (!found) cmd_output(state, "extension not found");
            cmd_finish(state);
            return true;
        }

        /* /ext reload */
        if (strcmp(subcmd, "reload") == 0) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "reloading extensions...");
            pthread_mutex_unlock(&state->mutex);

            if (state->rig && state->rig->api) {
                /* Free existing Lua extension states */
                RigExtensionAPI *api = state->rig->api;
                for (int i = api->extension_count - 1; i >= 0; i--) {
                    Extension *ext = api->extensions[i];
                    if (ext && ext->is_lua && ext->lua_state) {
                        lua_ext_free((LuaExtState *)ext->lua_state);
                        ext->lua_state = NULL;
                        free(ext->name);
                        free(ext->path);
                        free(ext);
                        api->extensions[i] = api->extensions[api->extension_count - 1];
                        api->extension_count--;
                    }
                }

                /* Re-discover and load */
                const char *global_dir = config_agent_dir();
                const char *pd = config_project_dir();
                extension_discover_and_load(api, pd, global_dir);

                /* Re-wire context */
                RigLuaContext reload_ctx = {
                    .agent = state->agent,
                    .store = state->store,
                    .model = state->model,
                    .api_key = state->api_key,
                    .cwd = state->cwd,
                    .mutex = &state->mutex,
                    .running = &state->running,
                };
                for (int i = 0; i < api->extension_count; i++) {
                    Extension *ext = api->extensions[i];
                    if (ext && ext->is_lua && ext->lua_state) {
                        lua_ext_set_context((LuaExtState *)ext->lua_state, &reload_ctx);
                    }
                }
            }

            pthread_mutex_lock(&state->mutex);
            char lbuf[128];
            snprintf(lbuf, sizeof(lbuf), "loaded %d extensions",
                     state->rig && state->rig->api ? state->rig->api->extension_count : 0);
            cmd_output(state, lbuf);
            cmd_finish(state);
            return true;
        }

        /* /ext edit <name> */
        if (strcmp(subcmd, "edit") == 0) {
            if (!subarg) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "usage: /ext edit <name>");
                cmd_finish(state);
                return true;
            }
            const char *editor = getenv("EDITOR");
            if (!editor) editor = "vi";

            /* Find extension path */
            const char *ext_path = NULL;
            if (state->rig && state->rig->api) {
                for (int i = 0; i < state->rig->api->extension_count; i++) {
                    Extension *ext = state->rig->api->extensions[i];
                    if (ext && ext->name && strstr(ext->name, subarg) && ext->path) {
                        ext_path = ext->path;
                        break;
                    }
                }
            }
            if (!ext_path) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "extension not found");
                cmd_finish(state);
                return true;
            }

            /* Exit alt screen, run editor, re-enter */
            terminal_disable_mouse();
            terminal_disable_bracketed_paste();
            terminal_disable_kitty_keyboard();
            terminal_exit_raw_mode();
            terminal_exit_alt_screen();

            char edit_cmd[512];
            snprintf(edit_cmd, sizeof(edit_cmd), "%s '%s'", editor, ext_path);
            system(edit_cmd);

            terminal_enter_alt_screen();
            terminal_enter_raw_mode();
            terminal_enable_kitty_keyboard();
            terminal_enable_bracketed_paste();
            terminal_enable_mouse();

            int tw, th;
            terminal_get_size(&tw, &th);
            viewport_resize(state->viewport, tw, th);
            viewport_render_full(state->viewport);

            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "editor closed — use /ext reload to apply changes");
            cmd_finish(state);
            return true;
        }

        /* /ext add <url> */
        if (strcmp(subcmd, "add") == 0) {
            if (!subarg) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "usage: /ext add <url>");
                cmd_finish(state);
                return true;
            }

            const char *pd = config_project_dir();
            char ext_dir[512];
            snprintf(ext_dir, sizeof(ext_dir), "%s/extensions", pd ? pd : ".");
            fs_mkdir_p(ext_dir);

            /* Determine if git URL or direct file */
            bool is_git = (strstr(subarg, ".git") != NULL);

            if (is_git) {
                char clone_cmd[1024];
                snprintf(clone_cmd, sizeof(clone_cmd), "git clone --depth 1 '%s' '%s/'", subarg, ext_dir);
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "cloning...");
                pthread_mutex_unlock(&state->mutex);

                Str output = str_new(1024);
                ProcessOptions popts = {
                    .command = clone_cmd,
                    .cwd = state->cwd,
                    .timeout_ms = 30000,
                    .on_stdout = collect_output,
                    .on_stderr = collect_output,
                    .ctx = &output,
                };
                ProcessResult presult;
                process_run(&popts, &presult);

                pthread_mutex_lock(&state->mutex);
                if (presult.exit_code == 0) {
                    cmd_output(state, "cloned — use /ext reload to load");
                } else {
                    cmd_output(state, "clone failed");
                    if (output.len > 0) linestore_add_tool_output(state->store, output.data);
                }
                str_free(&output);
            } else {
                /* Download single file with curl */
                const char *filename = strrchr(subarg, '/');
                filename = filename ? filename + 1 : "extension.lua";

                char dest[512];
                snprintf(dest, sizeof(dest), "%s/%s", ext_dir, filename);

                char dl_cmd[1024];
                snprintf(dl_cmd, sizeof(dl_cmd), "curl -sL -o '%s' '%s'", dest, subarg);

                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "downloading...");
                pthread_mutex_unlock(&state->mutex);

                Str output = str_new(1024);
                ProcessOptions popts = {
                    .command = dl_cmd,
                    .cwd = state->cwd,
                    .timeout_ms = 15000,
                    .on_stdout = collect_output,
                    .on_stderr = collect_output,
                    .ctx = &output,
                };
                ProcessResult presult;
                process_run(&popts, &presult);

                pthread_mutex_lock(&state->mutex);
                if (presult.exit_code == 0 && fs_exists(dest)) {
                    char lbuf[256];
                    snprintf(lbuf, sizeof(lbuf), "saved %s — use /ext reload to load", filename);
                    cmd_output(state, lbuf);
                } else {
                    cmd_output(state, "download failed");
                }
                str_free(&output);
            }
            cmd_finish(state);
            return true;
        }

        /* /ext build removed — model uses introspect ext_api + write tool instead */
        if (0) {
            if (!subarg) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "usage: /ext build <description of what the extension should do>");
                cmd_finish(state);
                return true;
            }
            if (!state->model || !state->api_key) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "no model configured");
                cmd_finish(state);
                return true;
            }

            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "generating extension...");
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
            viewport_render_full(state->viewport);

            /* Build the prompt */
            char *user_prompt = malloc(strlen(subarg) + 128);
            if (!user_prompt) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "out of memory");
                cmd_finish(state);
                return true;
            }
            snprintf(user_prompt, strlen(subarg) + 128,
                "Write a Rig Lua extension that does the following:\n%s", subarg);

            Message *msg = message_create_user(user_prompt);
            free(user_prompt);
            if (!msg) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "failed to create message");
                cmd_finish(state);
                return true;
            }

            Message flat = *msg;
            Str response = str_new(4096);
            ExtBuildBridge bridge = { .text = &response, .done = false };

            SimpleStreamOptions sopts = {
                .base = {
                    .temperature = 0.3,
                    .max_tokens = state->model->max_tokens,
                    .api_key = state->api_key,
                    .timeout_ms = 120000,
                    .abort_flag = &bridge.done,
                },
                .reasoning = THINKING_OFF,
            };

            ai_stream_simple(state->model, &flat, 1,
                            EXT_BUILD_SYSTEM_PROMPT, NULL, 0,
                            &sopts, ext_build_stream_cb, &bridge);
            message_free(msg);

            if (!response.data || response.len == 0) {
                pthread_mutex_lock(&state->mutex);
                cmd_output(state, "no response from model");
                str_free(&response);
                cmd_finish(state);
                return true;
            }

            /* Strip markdown fences if model included them */
            char *code = response.data;
            if (strncmp(code, "```lua\n", 7) == 0) code += 7;
            else if (strncmp(code, "```\n", 4) == 0) code += 4;
            char *fence_end = strstr(code, "\n```");
            if (fence_end) *fence_end = '\0';

            /* Show the generated code */
            pthread_mutex_lock(&state->mutex);
            linestore_add_blank(state->store);
            linestore_add_tool_output(state->store, code);
            linestore_add_blank(state->store);
            cmd_output(state, "save this extension? (y/n)");
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);

            /* Wait for y/n */
            struct pollfd pfd2 = { .fd = STDIN_FILENO, .events = POLLIN };
            bool confirmed = false;
            while (1) {
                int ready = poll(&pfd2, 1, 100);
                if (ready > 0) {
                    char ch[16];
                    ssize_t n = read(STDIN_FILENO, ch, sizeof(ch) - 1);
                    if (n > 0) {
                        if (ch[0] == 'y' || ch[0] == 'Y') { confirmed = true; break; }
                        if (ch[0] == 'n' || ch[0] == 'N' || ch[0] == '\x1b' || ch[0] == '\x03') break;
                    }
                }
            }

            pthread_mutex_lock(&state->mutex);
            if (!confirmed) {
                cmd_output(state, "cancelled");
                str_free(&response);
                cmd_finish(state);
                return true;
            }

            /* Generate filename from description */
            char filename[128] = {0};
            int fi = 0;
            for (const char *p = subarg; *p && fi < 60; p++) {
                char c = *p;
                if (c >= 'a' && c <= 'z') filename[fi++] = c;
                else if (c >= 'A' && c <= 'Z') filename[fi++] = (char)(c + 32);
                else if (c >= '0' && c <= '9') filename[fi++] = c;
                else if (c == ' ' && fi > 0 && filename[fi-1] != '-') filename[fi++] = '-';
            }
            while (fi > 0 && filename[fi-1] == '-') fi--;
            filename[fi] = '\0';
            if (fi == 0) strcpy(filename, "extension");

            /* Save to .rig/extensions/ */
            const char *pd = config_project_dir();
            char ext_dir[512];
            snprintf(ext_dir, sizeof(ext_dir), "%s/extensions", pd ? pd : ".");
            fs_mkdir_p(ext_dir);

            char save_path[512];
            snprintf(save_path, sizeof(save_path), "%s/%s.lua", ext_dir, filename);

            fs_write_file(save_path, code, strlen(code));

            char lbuf[256];
            snprintf(lbuf, sizeof(lbuf), "saved to %s", save_path);
            cmd_output(state, lbuf);
            cmd_output(state, "use /ext reload to load it");

            str_free(&response);
            cmd_finish(state);
            return true;
        }

        pthread_mutex_lock(&state->mutex);
        cmd_output(state, "usage: /ext [add|list|remove|reload|edit]");
        cmd_finish(state);
        return true;
    }

    /* Check Lua-registered commands */
    if (state->rig && state->rig->api) {
        RigExtensionAPI *eapi = state->rig->api;
        for (int i = 0; i < eapi->command_count; i++) {
            if (eapi->commands[i].name && strcmp(eapi->commands[i].name, cmd) == 0) {
                /* Build args array */
                const char *args_arr[16] = {0};
                int argc = 0;
                if (arg) {
                    /* Split arg on spaces — simple tokenizer */
                    char *arg_copy = strdup(arg);
                    char *tok = strtok(arg_copy, " ");
                    while (tok && argc < 16) {
                        args_arr[argc++] = tok;
                        tok = strtok(NULL, " ");
                    }
                    eapi->commands[i].handler(args_arr, argc, eapi->commands[i].ctx);
                    free(arg_copy);
                } else {
                    eapi->commands[i].handler(args_arr, 0, eapi->commands[i].ctx);
                }
                pthread_mutex_lock(&state->mutex);
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
                input_clear(state);
                sync_input_to_renderer(state);
                return true;
            }
        }
    }

    /* Unknown command */
    pthread_mutex_lock(&state->mutex);
    char buf[128];
    snprintf(buf, sizeof(buf), "unknown command: /%s — try /help", cmd);
    linestore_add_error(state->store, buf);
    cmd_finish(state);
    return true;
}

/* ---- Key Processing ---- */

static bool handle_key(InteractiveState *state, const ParsedKey *key) {
    LOG_DEBUG("KEY: id='%s' phase=%d raw_len=%d", key->id, state->phase, key->raw_len);
    if (key_matches(key, "scrollup")) {
        viewport_scroll_up(state->viewport, 3);
        return true;
    }
    if (key_matches(key, "scrolldown")) {
        viewport_scroll_down(state->viewport, 3);
        return true;
    }
    if (key_matches(key, "pageup")) {
        viewport_scroll_up(state->viewport, state->viewport->term_height - 2);
        return true;
    }
    if (key_matches(key, "pagedown")) {
        viewport_scroll_down(state->viewport, state->viewport->term_height - 2);
        return true;
    }
    if (key_matches(key, "ctrl+u")) {
        viewport_scroll_up(state->viewport, state->viewport->term_height / 2);
        return true;
    }
    if (key_matches(key, "ctrl+d")) {
        viewport_scroll_down(state->viewport, state->viewport->term_height / 2);
        return true;
    }
    if (key_matches(key, "end")) {
        viewport_scroll_to_bottom(state->viewport);
        return true;
    }
    if (key_matches(key, "shift+up")) {
        viewport_scroll_up(state->viewport, 3);
        return true;
    }
    if (key_matches(key, "shift+down")) {
        viewport_scroll_down(state->viewport, 3);
        return true;
    }
    if (key_matches(key, "up")) {
        if (state->phase == ISTATE_IDLE) {
            history_up(state);
        } else {
            viewport_scroll_up(state->viewport, 3);
        }
        return true;
    }
    if (key_matches(key, "down")) {
        if (state->phase == ISTATE_IDLE) {
            history_down(state);
        } else {
            viewport_scroll_down(state->viewport, 3);
        }
        return true;
    }
    if (key_matches(key, "enter")) {
        if (state->phase == ISTATE_IDLE) {
            if (state->input_len > 0 && state->input_buf[0] == '/') {
                history_push(state, state->input_buf);
                handle_slash_command(state);
            } else {
                handle_submit(state);
            }
        }
        return true;
    }
    if (key_matches(key, "ctrl+c")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            state->viewport->is_streaming = false;
            pthread_mutex_lock(&state->mutex);
            linestore_flush_stream(state->store);
            linestore_add_system(state->store, "interrupted");
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        } else {
            state->running = false;
        }
        return true;
    }
    if (key_matches(key, "escape") || key_matches(key, "ctrl+z")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            state->viewport->is_streaming = false;

            pthread_mutex_lock(&state->mutex);
            /* If no assistant content yet, restore prompt to input */
            bool has_response = (state->store->count > state->store->stream_start_idx);
            if (!has_response && state->last_prompt) {
                /* Remove user text + blank we added */
                while (state->store->count > state->last_prompt_store_idx) {
                    state->store->count--;
                    state->store->total_screen_rows -= state->store->lines[state->store->count].wrap_count;
                    free(state->store->lines[state->store->count].raw_text);
                    free(state->store->lines[state->store->count].spans);
                    state->store->lines[state->store->count].raw_text = NULL;
                    state->store->lines[state->store->count].spans = NULL;
                }
                input_insert(state, state->last_prompt, (int)strlen(state->last_prompt));
                sync_input_to_renderer(state);
            } else {
                linestore_flush_stream(state->store);
                linestore_add_system(state->store, "interrupted");
            }
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        return true;
    }
    if (key_matches(key, "ctrl+l")) {
        viewport_render_full(state->viewport);
        return true;
    }
    if (key_matches(key, "backspace")) {
        input_backspace(state);
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+a") || key_matches(key, "home")) {
        state->input_cursor = 0;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+e")) {
        state->input_cursor = state->input_len;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+u")) {
        memmove(state->input_buf, state->input_buf + state->input_cursor,
                state->input_len - state->input_cursor);
        state->input_len -= state->input_cursor;
        state->input_cursor = 0;
        state->input_buf[state->input_len] = '\0';
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+k")) {
        state->input_len = state->input_cursor;
        state->input_buf[state->input_len] = '\0';
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "left") || key_matches(key, "ctrl+b")) {
        if (state->input_cursor > 0) state->input_cursor--;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "right") || key_matches(key, "ctrl+f")) {
        if (state->input_cursor < state->input_len) state->input_cursor++;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }

    /* Printable characters */
    if (key->printable[0]) {
        input_insert(state, key->printable, (int)strlen(key->printable));
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }

    return false;
}

/* ---- Main Loop ---- */

static volatile sig_atomic_t g_winch = 0;
static void winch_handler(int sig) { (void)sig; g_winch = 1; }

int interactive_mode_start(RigInstance *rig, const char *session_id,
                           const char *model_pattern, const char *provider) {
    if (!rig) return -1;

    const char *agent_dir = config_agent_dir();
    if (agent_dir) {
        fs_mkdir_p(agent_dir);
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/rig.log", agent_dir);
        rig_log_open(log_path);
        rig_log_set_level(LOG_DEBUG);
    }
    LOG_INFO("=== Pi starting ===");

    http_global_init();
    ai_registry_init();
    models_init();
    anthropic_register();
    openai_completions_register();
    openai_responses_register();
    google_provider_register();
    bedrock_provider_register();
    mistral_provider_register();

    /* Use saved auth provider if no --provider flag */
    const char *effective_provider = provider;
    if (!effective_provider) {
        effective_provider = auth_get_active_provider();
    }

    const Model *model = NULL;
    char *api_key = NULL;

    int all_count = 0;
    const Model **all_models = models_get_all(effective_provider, &all_count);
    for (int i = 0; i < all_count && !api_key; i++) {
        if (model_pattern) {
            if (!strstr(all_models[i]->id, model_pattern) &&
                !strstr(all_models[i]->name, model_pattern)) continue;
        }
        char *key = auth_get_api_key(all_models[i]->provider);
        if (key) { model = all_models[i]; api_key = key; }
    }

    LOG_INFO("Model: %s (%s)", model ? model->id : "none", model ? model->provider : "none");

    /* Create state */
    InteractiveState *state = calloc(1, sizeof(InteractiveState));
    state->rig = rig;
    state->model = model;
    state->api_key = api_key;
    state->phase = ISTATE_IDLE;
    state->running = true;
    pthread_mutex_init(&state->mutex, NULL);

    input_init(state);
    history_init(state);

    /* Agent */
    state->agent = agent_state_create();
    state->agent->model = model;
    state->agent->thinking_level = THINKING_OFF;

    getcwd(state->cwd, sizeof(state->cwd));
    state->tool_cap = 32;
    state->tools = calloc(state->tool_cap, sizeof(Tool));
    state->tools[state->tool_count++] = tool_bash_create(state->cwd);
    state->tools[state->tool_count++] = tool_read_create();
    state->tools[state->tool_count++] = tool_write_create();
    state->tools[state->tool_count++] = tool_edit_create();
    state->tools[state->tool_count++] = tool_grep_create();
    state->tools[state->tool_count++] = tool_introspect_create();

    state->agent->tools = state->tools;
    state->agent->tool_count = state->tool_count;
    state->agent->system_prompt = system_prompt_build(state->tools, state->tool_count, state->cwd);

    state->agent_config = (AgentLoopConfig){
        .model = model,
        .tool_execution = TOOL_EXEC_PARALLEL,
        .temperature = -1,
        .max_tokens = model ? model->max_tokens : 4096,
        .reasoning = THINKING_OFF,
        .api_key = state->api_key,
        .timeout_ms = 120000,
        .abort_flag = &state->agent->abort_requested,
        .before_tool_call = before_tool,
        .hooks = state->rig ? state->rig->api->hooks : NULL,
    };

    const char *proj_dir = config_project_dir();

    /* Read snapshot budget from settings */
    SettingsManager *sm = settings_create(
        config_settings_global_path(), config_settings_project_path());
    size_t snap_max = (size_t)settings_get_int(sm, "snapshots.max_mb", 64) * 1024 * 1024;
    size_t file_max = (size_t)settings_get_int(sm, "snapshots.file_max_mb", 1) * 1024 * 1024;
    settings_free(sm);

    state->turnlog = turnlog_create(proj_dir, snap_max);

    /* Permissions — read-only tools auto-trusted */
    state->perms = permissions_create();
    /* No auto-trust — all tools prompt by default */
    if (state->turnlog && file_max > 0) {
        state->turnlog->file_max_bytes = file_max;
    }

    /* Scan persisted snapshots (metadata only, no data loaded) */
    if (proj_dir && fs_is_dir(proj_dir)) {
        int scanned = turnlog_scan(state->turnlog);
        if (scanned > 0) {
            LOG_INFO("scanned %d turn snapshots from %s", scanned, proj_dir);
        }
    }

    /* Viewport */
    state->store = linestore_create();
    state->viewport = viewport_create(state->store);

    /* Wire Lua extension context */
    RigLuaContext lua_ctx = {
        .agent = state->agent,
        .store = state->store,
        .model = state->model,
        .api_key = state->api_key,
        .cwd = state->cwd,
        .mutex = &state->mutex,
        .running = &state->running,
    };
    if (state->rig && state->rig->api) {
        for (int i = 0; i < state->rig->api->extension_count; i++) {
            Extension *ext = state->rig->api->extensions[i];
            if (ext && ext->is_lua && ext->lua_state) {
                lua_ext_set_context((LuaExtState *)ext->lua_state, &lua_ctx);
            }
        }
    }

    /* Load project-local extensions */
    if (proj_dir) {
        extension_discover_and_load(state->rig->api, proj_dir, NULL);
        for (int i = 0; i < state->rig->api->extension_count; i++) {
            Extension *ext = state->rig->api->extensions[i];
            if (ext && ext->is_lua && ext->lua_state) {
                lua_ext_set_context((LuaExtState *)ext->lua_state, &lua_ctx);
            }
        }
    }

    /* Merge extension-registered tools into agent's tool array */
    if (state->rig && state->rig->api) {
        RigExtensionAPI *eapi = state->rig->api;
        for (int i = 0; i < eapi->tool_count; i++) {
            Tool *et = eapi->tools[i];
            if (!et || !et->name) continue;

            /* Skip if already in agent tools (by name) */
            bool exists = false;
            for (int j = 0; j < state->tool_count; j++) {
                if (state->tools[j].name && strcmp(state->tools[j].name, et->name) == 0) {
                    exists = true; break;
                }
            }
            if (exists) continue;

            /* Grow if needed */
            if (state->tool_count >= state->tool_cap) {
                int new_cap = state->tool_cap * 2;
                Tool *nt = realloc(state->tools, (size_t)new_cap * sizeof(Tool));
                if (!nt) continue;
                state->tools = nt;
                state->tool_cap = new_cap;
            }

            /* Copy metadata, set Lua execute bridge */
            Tool *dest = &state->tools[state->tool_count];
            memset(dest, 0, sizeof(Tool));
            dest->name = strdup(et->name);
            dest->description = et->description ? strdup(et->description) : NULL;
            dest->label = et->label ? strdup(et->label) : NULL;
            dest->parameters = et->parameters ? cJSON_Duplicate(et->parameters, 1) : NULL;
            dest->execute = lua_tool_execute;
            state->tool_count++;
        }

        /* Update agent with expanded tool list */
        state->agent->tools = state->tools;
        state->agent->tool_count = state->tool_count;
        free(state->agent->system_prompt);
        state->agent->system_prompt = system_prompt_build(
            state->tools, state->tool_count, state->cwd);

        LOG_INFO("agent has %d tools (%d from extensions)",
                 state->tool_count, state->tool_count - 7);
    }

    /* Set introspect tool context */
    introspect_tool_set_context(
        state->rig ? state->rig->api : NULL,
        state->perms,
        state->tools, state->tool_count,
        state->cwd);

    /* Session */
    const char *sessions_dir = config_sessions_dir();
    if (session_id) {
        Str path = str_new(256);
        str_appendf(&path, "%s/%s.jsonl", sessions_dir, session_id);
        state->session = session_load(path.data);
        str_free(&path);
        if (!state->session) {
            state->session = session_create(sessions_dir);
        }
    } else {
        state->session = session_create(sessions_dir);
    }

    /* Restore session if loading existing one */
    if (session_id) {
        restore_session(state);
    }

    /* Terminal setup */
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: Interactive mode requires a terminal.\n");
        free(state);
        return -1;
    }

    /* Trust gate — ask before creating .rig/ in a new directory */
    if (proj_dir && !fs_exists(proj_dir)) {
        fprintf(stderr, "rig: first run in %s\n", state->cwd);
        fprintf(stderr, "trust this directory? [y/N] ");
        fflush(stderr);
        char answer[16] = {0};
        if (fgets(answer, sizeof(answer), stdin)) {
            if (answer[0] != 'y' && answer[0] != 'Y') {
                fprintf(stderr, "aborted\n");
                free(state->input_buf);
                free(state->api_key);
                if (state->agent) agent_state_free(state->agent);
                if (state->session) session_free(state->session);
                turnlog_free(state->turnlog);
                viewport_free(state->viewport);
                linestore_free(state->store);
                pthread_mutex_destroy(&state->mutex);
                free(state);
                return 1;
            }
        }
        fs_mkdir_p(proj_dir);
        LOG_INFO("created project directory: %s", proj_dir);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = winch_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    terminal_enter_alt_screen();
    terminal_enter_raw_mode();
    terminal_enable_kitty_keyboard();
    terminal_enable_bracketed_paste();
    terminal_enable_mouse();

    int tw, th;
    terminal_get_size(&tw, &th);
    viewport_resize(state->viewport, tw, th);

    /* Startup splash */
    show_splash(state);

    if (!model || !api_key) {
        linestore_add_error(state->store, "no API key configured");
        linestore_add_system(state->store,
            "set ANTHROPIC_ARIG_KEY, OPENAI_ARIG_KEY, GOOGLE_ARIG_KEY, MISTRAL_ARIG_KEY, or AWS_BEARER_TOKEN_BEDROCK");
        linestore_add_blank(state->store);
    }

    viewport_render_full(state->viewport);

    /* Event loop */
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    int tick_counter = 0;

    while (state->running) {
        if (g_winch) {
            g_winch = 0;
            terminal_get_size(&tw, &th);
            viewport_resize(state->viewport, tw, th);
            viewport_render_full(state->viewport);
        }

        int ready = poll(&pfd, 1, 16);

        if (ready > 0 && (pfd.revents & POLLIN) && !state->permission_pending) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                ParsedKey key = key_parse(buf, (int)n);
                handle_key(state, &key);
                if (!state->running) break;
            }
        }

        /* Spinner tick every ~200ms (12 frames * 16ms) */
        tick_counter++;
        if (tick_counter >= 12) {
            tick_counter = 0;
            viewport_tick_spinner(state->viewport);
        }

        pthread_mutex_lock(&state->mutex);
        bool do_render = state->needs_render || state->viewport->dirty;
        bool render_blocked = state->permission_pending;
        state->needs_render = false;
        pthread_mutex_unlock(&state->mutex);
        if (do_render && !render_blocked) {
            viewport_render(state->viewport);
        }
    }

    /* Cleanup */
    terminal_disable_mouse();
    terminal_disable_bracketed_paste();
    terminal_disable_kitty_keyboard();
    terminal_show_cursor();
    terminal_exit_raw_mode();
    terminal_exit_alt_screen();

    viewport_free(state->viewport);
    linestore_free(state->store);

    if (state->agent) agent_state_free(state->agent);
    if (state->session) session_free(state->session);
    pthread_mutex_destroy(&state->mutex);
    history_free(state);
    free(state->input_buf);
    free(state->tools);
    if (state->perms) permissions_free(state->perms);
    free(state->api_key);
    free(state->last_prompt);
    /* Flush any in-memory turn to disk before exit */
    if (state->turnlog) {
        turnlog_flush(state->turnlog);
        turnlog_free(state->turnlog);
    }
    free(state);

    ai_registry_cleanup();
    http_global_cleanup();
    rig_log_close();

    return 0;
}
