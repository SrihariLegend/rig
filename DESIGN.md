# Pi-C: Pi Coding Agent in C

## Vision

Rewrite of [pi-mono](https://github.com/badlogic/pi-mono) — a minimal, extensible terminal coding agent — in C (with Rust as fallback if C fights too hard).

Pi's core philosophy: **adapt Pi to your workflows, not the other way around.** Powerful defaults, skip opinionated features (no built-in sub-agents or plan mode). Four modes: interactive, print/JSON, RPC, SDK. Extensible via skills, prompt templates, themes, and packages.

This document maps every architectural subsystem from the TypeScript original to a C implementation plan. Nothing skipped.

---

## Table of Contents

1. [Original Architecture Summary](#1-original-architecture-summary)
2. [Package Mapping](#2-package-mapping)
3. [Core Types & Data Model](#3-core-types--data-model)
4. [pi-ai: Unified LLM API](#4-pi-ai-unified-llm-api)
5. [pi-agent: Agent Loop](#5-pi-agent-agent-loop)
6. [pi-coding-agent: The Harness](#6-pi-coding-agent-the-harness)
7. [pi-tui: Terminal UI](#7-pi-tui-terminal-ui)
8. [Extension, Plugin & Workflow System](#8-extension-plugin--workflow-system)
   - 8.1 Extension Loading: Three Tiers
   - 8.2 Core Workflow Engine
   - 8.3 Event Bus — Inter-Extension Communication
   - 8.4 Programmable Hooks — Beyond Simple Events
   - 8.5 Custom Modes
   - 8.6 Programmable Context Window
   - 8.7 Tool Composition
   - 8.8 Full PiExtensionAPI
   - 8.9 Discovery & Loading
   - 8.10 Example Workflows
9. [Build System & Dependencies](#9-build-system--dependencies)
10. [C vs Rust Decision Points](#10-c-vs-rust-decision-points)
11. [Implementation Phases](#11-implementation-phases)
12. [Risk Register](#12-risk-register)

---

## 1. Original Architecture Summary

### Package Overview

| TS Package | Lines | Purpose |
|-----------|-------|---------|
| `pi-ai` | 29K | Unified multi-provider LLM API (OpenAI, Anthropic, Google, Bedrock, Mistral, etc.) |
| `pi-agent` | 2K | Agent loop: stream → tool calls → execute → loop |
| `pi-coding-agent` | 45K | Full CLI: sessions, tools, modes, extensions, settings, bash executor |
| `pi-tui` | 11K | Terminal UI: differential rendering, widgets, overlays, keyboard/mouse |
| `pi-web-ui` | — | Web components (out of scope for C rewrite) |
| `pi-mom` | — | Slack bot (out of scope) |
| `pi-pods` | — | GPU pod management (out of scope) |

### What Makes Pi Special

1. **Minimal harness, maximum extensibility** — core is small, everything else pluggable
2. **Four modes** — interactive TUI, print (single-shot), JSON (structured output), RPC (editor integration), SDK (embedding)
3. **Skills** — Markdown files following Agent Skills standard, progressively disclosed to model
4. **Prompt templates** — `/name arg1 arg2` expansion with `$1`, `$2`, `$@` substitution
5. **Themes** — 51-token JSON color schemes with hot reload
6. **Extensions** — Full lifecycle hooks (30+ events), tool/command/provider registration
7. **Packages** — Bundle extensions/skills/prompts/themes, distribute via npm/git
8. **Session tree** — Append-only JSONL with branching, forking, compaction
9. **Provider normalization** — Unified content model across all LLM providers
10. **No opinionated features** — No built-in sub-agents, no plan mode. Build what you want.

---

## 2. Package Mapping

### C Equivalent Structure

```
pi-c/
├── DESIGN.md              # This file
├── Makefile               # Build system (or CMake)
├── deps/                  # Vendored dependencies
│   ├── cJSON/             # JSON parsing
│   ├── libuv/             # Async I/O event loop
│   ├── libcurl/           # HTTP client
│   ├── pcre2/             # Regex (for grep tool)
│   ├── utf8proc/          # Unicode handling
│   ├── lua/               # Lua 5.4 (scripted extensions)
│   └── libyaml/           # YAML parsing (declarative workflows)
├── src/
│   ├── ai/                # ≡ pi-ai: LLM provider abstraction
│   │   ├── types.h        # Messages, tools, models, content types
│   │   ├── types.c
│   │   ├── stream.h       # Event stream, SSE parsing
│   │   ├── stream.c
│   │   ├── registry.h     # Provider registry
│   │   ├── registry.c
│   │   ├── models.h       # Model definitions, cost calculation
│   │   ├── models.c
│   │   ├── validation.h   # Tool argument validation
│   │   ├── validation.c
│   │   ├── json_parse.h   # Robust/streaming JSON
│   │   ├── json_parse.c
│   │   ├── providers/     # Per-provider implementations
│   │   │   ├── anthropic.c
│   │   │   ├── openai_completions.c
│   │   │   ├── openai_responses.c
│   │   │   ├── google.c
│   │   │   ├── bedrock.c
│   │   │   └── mistral.c
│   │   └── transform.c    # Cross-provider message normalization
│   ├── agent/             # ≡ pi-agent: agent loop
│   │   ├── agent.h
│   │   ├── agent.c        # Agent state, prompt/continue/abort
│   │   ├── loop.h
│   │   ├── loop.c         # Core loop: stream → tools → iterate
│   │   └── proxy.c        # Proxy streaming (partial reconstruction)
│   ├── harness/           # ≡ pi-coding-agent: the coding agent
│   │   ├── session.h      # Session management, JSONL persistence
│   │   ├── session.c
│   │   ├── settings.h     # 3-layer settings (global/project/CLI)
│   │   ├── settings.c
│   │   ├── model_registry.h
│   │   ├── model_registry.c
│   │   ├── extensions/    # Extension system
│   │   │   ├── loader.h   # Discovery & loading (.so, .lua, .yaml)
│   │   │   ├── loader.c
│   │   │   ├── runner.h   # Event dispatch, hook chains
│   │   │   ├── runner.c
│   │   │   ├── types.h    # Extension API (PiExtensionAPI)
│   │   │   ├── lua_bindings.h  # Lua ↔ Pi API bridge
│   │   │   ├── lua_bindings.c
│   │   │   ├── bus.h      # Event bus (pub/sub/request-reply)
│   │   │   ├── bus.c
│   │   │   ├── policy.h   # Composable policy engine
│   │   │   └── policy.c
│   │   ├── workflow/      # Workflow engine
│   │   │   ├── types.h    # Workflow, Step, Context types
│   │   │   ├── parser.h   # YAML/JSON → Workflow graph
│   │   │   ├── parser.c
│   │   │   ├── executor.h # DAG execution engine
│   │   │   ├── executor.c
│   │   │   ├── variables.h # Variable system & expression eval
│   │   │   ├── variables.c
│   │   │   ├── gates.h    # Gate system (confirm, webhook, custom)
│   │   │   ├── gates.c
│   │   │   ├── spawn.h    # Sub-agent session spawning
│   │   │   ├── spawn.c
│   │   │   ├── checkpoint.h # State serialization & resume
│   │   │   └── checkpoint.c
│   │   ├── tools/         # Built-in tools
│   │   │   ├── bash.c     # Shell execution
│   │   │   ├── read.c     # File reading
│   │   │   ├── write.c    # File writing
│   │   │   ├── edit.c     # File editing (search/replace + diff)
│   │   │   ├── grep.c     # ripgrep wrapper
│   │   │   ├── find.c     # fd wrapper
│   │   │   └── ls.c       # Directory listing
│   │   ├── skills.c       # Agent Skills standard loader
│   │   ├── prompts.c      # Prompt template expansion
│   │   ├── themes.c       # Theme loading & hot reload
│   │   ├── packages.c     # Package manager (install/remove/update)
│   │   ├── compaction.c   # Context compaction
│   │   ├── system_prompt.c# System prompt builder
│   │   ├── auth.c         # API key & OAuth credential storage
│   │   ├── config.c       # Path resolution (~/.pi/agent/*, .pi/*)
│   │   └── modes/
│   │       ├── interactive.c  # Full TUI mode
│   │       ├── print.c       # Single-shot print/JSON mode
│   │       └── rpc.c         # JSONL stdio RPC mode
│   ├── tui/               # ≡ pi-tui: terminal UI
│   │   ├── tui.h          # Differential rendering engine
│   │   ├── tui.c
│   │   ├── terminal.h     # Raw mode, escape sequences
│   │   ├── terminal.c
│   │   ├── keys.h         # Keyboard input parser (Kitty protocol)
│   │   ├── keys.c
│   │   ├── widgets/       # Widget implementations
│   │   │   ├── text.c     # Word-wrapped text
│   │   │   ├── markdown.c # Markdown renderer
│   │   │   ├── input.c    # Text input (emacs keys, kill ring)
│   │   │   ├── editor.c   # Multi-line editor
│   │   │   ├── select.c   # Selection list
│   │   │   ├── box.c      # Container with padding
│   │   │   ├── image.c    # Kitty/iTerm2 inline images
│   │   │   └── loader.c   # Animated spinner
│   │   ├── overlay.c      # Overlay positioning & compositing
│   │   ├── ansi.c         # ANSI code tracking & width calculation
│   │   └── unicode.c      # Grapheme segmentation, East Asian width
│   ├── util/              # Shared utilities
│   │   ├── arena.h        # Arena allocator
│   │   ├── arena.c
│   │   ├── str.h          # Dynamic string builder
│   │   ├── str.c
│   │   ├── vec.h          # Dynamic array (generic via macros)
│   │   ├── hashmap.h      # Hash map
│   │   ├── hashmap.c
│   │   ├── json.h         # cJSON wrapper with path access
│   │   ├── json.c
│   │   ├── http.h         # libcurl wrapper for SSE
│   │   ├── http.c
│   │   ├── fs.h           # File operations
│   │   ├── fs.c
│   │   ├── process.h      # Child process management
│   │   ├── process.c
│   │   └── log.h          # Logging
│   └── main.c             # Entry point, CLI arg parsing
├── include/               # Public headers (for SDK/embedding)
│   └── pi.h               # Single-header SDK API
└── test/
    ├── test_ai.c
    ├── test_agent.c
    ├── test_session.c
    └── test_tui.c
```

---

## 3. Core Types & Data Model

### Message Types

The original uses a rich discriminated union. In C, use tagged unions:

```c
// Content block types
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
        struct { char *data; char *mime_type; } image;          // base64
        struct { char *id; char *name; cJSON *arguments; char *partial_json; char *thought_signature; } tool_call;
    };
} ContentBlock;

// Message roles
typedef enum {
    ROLE_USER,
    ROLE_ASSISTANT,
    ROLE_TOOL_RESULT,
} MessageRole;

typedef struct {
    MessageRole role;
    ContentBlock *content;      // dynamic array
    int content_count;
    int64_t timestamp;          // unix ms

    // Assistant-only fields
    char *api;
    char *provider;
    char *model;
    char *response_id;
    struct {
        int input_tokens;
        int output_tokens;
        int cache_read_tokens;
        int cache_write_tokens;
        int total_tokens;
        struct {
            double input;
            double output;
            double cache_read;
            double cache_write;
            double total;
        } cost;                     // dollar cost per category
    } usage;
    enum { STOP_STOP, STOP_LENGTH, STOP_TOOL_USE, STOP_ERROR, STOP_ABORTED } stop_reason;
    char *error_message;

    // ToolResult-only fields
    char *tool_call_id;
    char *tool_name;
    cJSON *details;
    bool is_error;
} Message;

// Agent message = Message + coding-agent-specific types
typedef enum {
    AGENT_MSG_LLM,              // standard user/assistant/toolResult
    AGENT_MSG_BASH_EXECUTION,   // bash tool output with streaming chunks
    AGENT_MSG_CUSTOM,           // extension-injected messages (display in context)
    AGENT_MSG_BRANCH_SUMMARY,   // summary when navigating session tree branches
    AGENT_MSG_COMPACTION,       // compaction summary replacing older messages
} AgentMessageKind;

typedef struct {
    AgentMessageKind kind;
    union {
        Message llm;
        struct { char *command; ContentBlock *chunks; int chunk_count; int exit_code; } bash_execution;
        struct { char *type; cJSON *data; bool display; } custom;
        struct { char *summary; char *from_entry_id; } branch_summary;
        struct { char *summary; char *first_kept_entry_id; } compaction;
    };
} AgentMessage;
```

### Tool Definition

```c
typedef struct Tool Tool;
struct Tool {
    char *name;
    char *label;
    char *description;
    cJSON *parameters;          // JSON Schema

    // Function pointers (C vtable pattern)
    int (*prepare_arguments)(cJSON *args, cJSON **out);
    int (*execute)(const char *call_id, cJSON *params, /*AbortSignal*/void *signal,
                   void (*on_update)(void *ctx, cJSON *partial), void *ctx,
                   /* out */ ContentBlock **content, int *content_count,
                   cJSON **details, bool *terminate);
    enum { EXEC_SEQUENTIAL, EXEC_PARALLEL, EXEC_DEFAULT } execution_mode;
};
```

### Model Definition

```c
typedef struct {
    char *id;
    char *name;
    char *api;                  // "anthropic-messages", "openai-completions", etc.
    char *provider;             // "anthropic", "openai", etc.
    char *base_url;
    bool reasoning;
    const char **input_modalities;  // e.g., ["text", "image"], NULL-terminated
    int input_modality_count;
    struct { double input, output, cache_read, cache_write; } cost;  // per million tokens
    int context_window;
    int max_tokens;
    cJSON *headers;             // optional custom headers
    // Provider-specific compatibility flags (typed, not opaque JSON)
    union {
        struct {
            bool supports_store;
            bool supports_developer_role;
            bool supports_reasoning_effort;
            bool supports_usage_in_streaming;
            bool supports_strict_mode;
            bool supports_long_cache_retention;
            bool send_session_affinity_headers;
            char *thinking_format;          // "openai"|"openrouter"|"deepseek"|"zai"|"qwen"|"qwen-chat-template"
            char *cache_control_format;     // "anthropic" for OpenRouter Anthropic models
            char *max_tokens_field;         // field name override for max_tokens
            bool requires_tool_result_name;
            bool requires_assistant_after_tool_result;
            bool requires_thinking_as_text;
            bool requires_reasoning_content_on_assistant;
            bool zai_tool_stream;           // z.ai tool streaming quirk
            cJSON *open_router_routing;     // routing hints
            cJSON *vercel_gateway_routing;
        } openai_completions;
        struct {
            bool send_session_id_header;
            bool supports_long_cache_retention;
        } openai_responses;
        struct {
            bool supports_eager_tool_input_streaming;
            bool supports_long_cache_retention;
        } anthropic;
    } compat;
    enum { COMPAT_NONE, COMPAT_OPENAI_COMPLETIONS, COMPAT_OPENAI_RESPONSES, COMPAT_ANTHROPIC } compat_type;
} Model;
```

### Event Stream

```c
// Streaming events from LLM
typedef enum {
    EVENT_START,
    EVENT_TEXT_START, EVENT_TEXT_DELTA, EVENT_TEXT_END,
    EVENT_THINKING_START, EVENT_THINKING_DELTA, EVENT_THINKING_END,
    EVENT_TOOLCALL_START, EVENT_TOOLCALL_DELTA, EVENT_TOOLCALL_END,
    EVENT_DONE,
    EVENT_ERROR,
} StreamEventType;

typedef struct {
    StreamEventType type;
    int content_index;
    char *delta;                // for delta events
    char *stop_reason;          // for done/error
    char *error_message;
    Message *partial;           // running partial message
    Message *message;           // final message (done/error only)
} StreamEvent;

// Callback-based stream (C doesn't have async iterators)
typedef void (*StreamCallback)(StreamEvent *event, void *userdata);
```

### Agent Events

```c
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
    AgentMessage *message;      // for message events
    Message *tool_results;      // for turn_end
    int tool_results_count;
    char *tool_call_id;         // for tool exec events
    char *tool_name;
    cJSON *args;
    cJSON *result;
    bool is_error;
} AgentEvent;
```

---

## 4. pi-ai: Unified LLM API

### Architecture

**Registry pattern**: providers register stream functions keyed by API type.

```c
// Provider stream function signatures — each provider implements BOTH:
// - stream: accepts provider-specific options (e.g., AnthropicOptions)
// - streamSimple: accepts unified SimpleStreamOptions with reasoning level mapping
typedef int (*ProviderStreamFn)(
    const Model *model,
    const Message *messages, int msg_count,
    const char *system_prompt,
    const Tool *tools, int tool_count,
    const StreamOptions *options,
    StreamCallback callback, void *userdata
);

typedef int (*ProviderStreamSimpleFn)(
    const Model *model,
    const Message *messages, int msg_count,
    const char *system_prompt,
    const Tool *tools, int tool_count,
    const SimpleStreamOptions *options,    // unified reasoning interface
    StreamCallback callback, void *userdata
);

// SimpleStreamOptions extends StreamOptions with reasoning level
typedef struct {
    StreamOptions base;
    int reasoning;              // 0=off, 1=minimal, 2=low, 3=medium, 4=high, 5=xhigh
    cJSON *thinking_budgets;    // optional per-level token overrides {minimal:N, low:N, ...}
} SimpleStreamOptions;

// Registry — each provider registers both functions
typedef struct {
    const char *api;
    ProviderStreamFn stream;
    ProviderStreamSimpleFn stream_simple;
} ApiProvider;

void ai_register_provider(ApiProvider *provider);
ApiProvider *ai_get_provider(const char *api);
```

### Provider Implementations

Each provider (anthropic.c, openai_completions.c, etc.) must:

1. **Convert messages** to provider-native JSON format
2. **Build HTTP request** with proper headers, auth, body
3. **Parse SSE stream** line by line
4. **Emit normalized StreamEvents** via callback
5. **Handle provider quirks** via compat flags

#### SSE Parsing Core

All providers share SSE parsing. Implemented in `http.c`:

```c
// SSE line parser state machine
typedef struct {
    char *event_type;           // current "event:" value
    char *data_buffer;          // accumulated "data:" lines
    void (*on_event)(const char *type, const char *data, void *ctx);
    void *ctx;
} SSEParser;

void sse_parser_feed(SSEParser *p, const char *chunk, size_t len);
```

#### Anthropic Provider (anthropic.c)

Key responsibilities:
- OAuth detection (`sk-ant-oat` prefix) → add identity headers
- Adaptive thinking (Opus 4.6+, Sonnet 4.6): `effort` levels
- Budget-based thinking (older models): `thinkingBudgetTokens`
- Cache control: ephemeral markers with optional 1h TTL
- Message conversion: collapse consecutive tool results into single user message
- Handle redacted thinking blocks

#### OpenAI Completions Provider (openai_completions.c)

Key responsibilities:
- Developer role for reasoning models (vs system role)
- Reasoning effort mapping per provider quirks
- DeepSeek thinking format (`thinking.type: "enabled"`)
- Tool call delta merging by stream index
- OpenRouter routing headers

#### OpenAI Responses Provider (openai_responses.c)

Key responsibilities:
- 20+ event types from Responses API
- Reasoning items → ThinkingContent with signature
- Message items → TextContent with TextSignatureV1
- Function call items → ToolCall
- Session-based prompt caching

#### Message Transformation (transform.c)

Cross-provider normalization before sending to any provider:
- **Tool call ID normalization** — OpenAI IDs can be 450+ chars, Anthropic max 64
- **Thinking block handling** — same model: preserve signatures, different model: convert to text
- **Image downgrade** — non-vision models get placeholder text
- **Orphaned tool calls** — insert synthetic error results
- **Error message filtering** — skip assistant messages with error/aborted stopReason

### Models Database (models.c)

The original has 15K lines of auto-generated model definitions. For C:
- **Embed as static data** — array of Model structs compiled in
- **Code generator** — script reads models.generated.ts, emits models.c
- Or: load from JSON file at runtime (simpler to update)

### JSON Handling

Critical subsystem. LLM APIs are JSON-heavy.

```c
// Robust JSON parsing (json_parse.c)
cJSON *json_parse_repair(const char *json);           // fix malformed JSON
cJSON *json_parse_streaming(const char *partial);     // parse incomplete JSON

// Path-based access (json.c)
cJSON *json_get(cJSON *root, const char *path);       // "foo.bar[0].baz"
char  *json_get_string(cJSON *root, const char *path);
int    json_get_int(cJSON *root, const char *path, int default_val);
double json_get_double(cJSON *root, const char *path, double default_val);
bool   json_get_bool(cJSON *root, const char *path, bool default_val);
```

### Tool Argument Validation (validation.c)

TypeBox schemas in TS → JSON Schema in C. Validate tool call arguments against schema:
- Type checking (string, number, boolean, object, array)
- Type coercion (string→number, boolean→number)
- Required field checking
- Enum validation
- Nested object/array validation
- Error messages with JSON paths

---

## 5. pi-agent: Agent Loop

### Core Loop

The heart of Pi. Relatively small (~2K lines TS), maps cleanly to C.

```c
// Agent state
typedef struct {
    char *system_prompt;
    Model *model;
    int thinking_level;         // 0=off, 1=minimal, 2=low, 3=medium, 4=high, 5=xhigh
    Tool *tools;
    int tool_count;
    AgentMessage *messages;
    int message_count;
    bool is_streaming;
    AgentMessage *streaming_message;
    // ... abort handling, pending tool calls set
} AgentState;

// Agent loop config
typedef struct {
    Model *model;
    int (*convert_to_llm)(AgentMessage *msgs, int count, Message **out, int *out_count);
    int (*transform_context)(AgentMessage *msgs, int count, AgentMessage **out, int *out_count);
    int (*get_api_key)(const char *provider, char **key);
    int (*get_steering_messages)(AgentMessage **out, int *out_count);
    int (*get_follow_up_messages)(AgentMessage **out, int *out_count);
    int (*before_tool_call)(/* context */, bool *block, char **reason);
    int (*after_tool_call)(/* context */, /* overrides */);
    enum { TOOL_EXEC_SEQUENTIAL, TOOL_EXEC_PARALLEL } tool_execution;
    // stream options
    double temperature;
    int max_tokens;
    int reasoning;
    // ...
} AgentLoopConfig;

// Public API
int agent_prompt(AgentState *state, AgentMessage *messages, int count,
                 AgentLoopConfig *config, AgentEventCallback cb, void *userdata);
int agent_continue(AgentState *state, AgentLoopConfig *config,
                   AgentEventCallback cb, void *userdata);
void agent_abort(AgentState *state);
```

### Loop Algorithm (Pseudocode)

```
agent_prompt(state, prompts, config):
    add prompts to state->messages
    emit AGENT_START

    OUTER:
    while true:
        drain steering queue → pending_messages

        INNER:
        while has_more_tool_calls || pending_messages:
            emit TURN_START
            inject pending_messages into state->messages

            // Stream assistant response
            transform_context(state->messages)
            convert_to_llm(state->messages) → llm_messages
            provider_stream(model, llm_messages, ...) → assistant_message
            emit MESSAGE_START, MESSAGE_UPDATE*, MESSAGE_END

            // Execute tool calls
            tool_calls = extract_tool_calls(assistant_message)
            if tool_calls:
                for each tool_call:
                    emit TOOL_EXEC_START
                    prepare → validate → before_hook
                    execute tool
                    after_hook → finalize
                    emit TOOL_EXEC_END
                    create tool_result message
                has_more = !all_terminate

            emit TURN_END
            drain steering queue → pending_messages

        // Check follow-ups
        drain follow_up queue → follow_ups
        if follow_ups:
            pending_messages = follow_ups
            continue OUTER
        else:
            break OUTER

    emit AGENT_END
```

### Parallel Tool Execution

Use pthreads or libuv thread pool:

```c
// Sequential: straightforward loop
// Parallel: prepare all, then execute concurrently
typedef struct {
    Tool *tool;
    cJSON *args;
    char *call_id;
    ContentBlock *result_content;
    int result_count;
    cJSON *result_details;
    bool terminate;
    bool is_error;
} ToolExecJob;

void execute_tools_parallel(ToolExecJob *jobs, int count);
```

### Message Queues (Steering & Follow-Up)

```c
typedef struct {
    AgentMessage *items;
    int count;
    int capacity;
    enum { QUEUE_ALL, QUEUE_ONE_AT_A_TIME } mode;
} MessageQueue;

int  queue_enqueue(MessageQueue *q, AgentMessage *msg);
int  queue_drain(MessageQueue *q, AgentMessage **out, int *count);
void queue_clear(MessageQueue *q);
```

### Proxy Streaming (proxy.c)

For server-proxied LLM calls:
- POST to `${proxyUrl}/api/stream`
- Receive events WITHOUT `partial` field (bandwidth saving)
- Reconstruct partial message client-side
- Same event types, minus the `partial` on each event

---

## 6. pi-coding-agent: The Harness

### Session Management (session.c)

**Append-only JSONL** tree structure:

```c
typedef enum {
    ENTRY_MESSAGE,
    ENTRY_THINKING_LEVEL_CHANGE,
    ENTRY_MODEL_CHANGE,
    ENTRY_COMPACTION,
    ENTRY_BRANCH_SUMMARY,
    ENTRY_CUSTOM,
    ENTRY_CUSTOM_MESSAGE,
    ENTRY_LABEL,
    ENTRY_SESSION_INFO,
} SessionEntryType;

typedef struct {
    char *id;                   // unique entry ID
    char *parent_id;            // tree parent
    SessionEntryType type;
    cJSON *data;                // type-specific payload
} SessionEntry;

typedef struct {
    char *session_id;
    char *file_path;            // ~/.pi/agent/sessions/<id>.jsonl
    SessionEntry *entries;
    int entry_count;
    char *leaf_id;              // current position in tree
    int version;                // currently 3
} Session;

// Operations
Session *session_create(const char *id);
Session *session_load(const char *path);
int      session_append(Session *s, SessionEntry *entry);
int      session_branch(Session *s, const char *from_entry_id);
int      session_fork(Session *s, Session **new_session);
int      session_build_context(Session *s, AgentMessage **out, int *count);
```

### Settings Manager (settings.c)

**Three-layer merge** with file watching:

```c
// Nested settings structs
typedef struct {
    bool enabled;               // default: true
    int reserve_tokens;         // default: 16384
    int keep_recent_tokens;     // default: 20000
} CompactionSettings;

typedef struct {
    int reserve_tokens;         // default: 16384
    bool skip_prompt;           // default: false
} BranchSummarySettings;

typedef struct {
    int timeout_ms;             // SDK/provider request timeout
    int max_retries;            // SDK/provider retry attempts
    int max_retry_delay_ms;     // default: 60000
} ProviderRetrySettings;

typedef struct {
    bool enabled;               // default: true
    int max_retries;            // default: 3
    int base_delay_ms;          // default: 2000 (exponential backoff)
    ProviderRetrySettings provider;
} RetrySettings;

typedef struct {
    bool show_images;           // default: true
    int image_width_cells;      // default: 60
    bool clear_on_shrink;       // default: false
    bool show_terminal_progress;// default: false (OSC 9;4)
} TerminalSettings;

typedef struct {
    bool auto_resize;           // default: true (resize to 2000x2000 max)
    bool block_images;          // default: false
} ImageSettings;

typedef struct {
    int minimal;                // optional token budget overrides
    int low;
    int medium;
    int high;
} ThinkingBudgetsSettings;

typedef struct {
    char *code_block_indent;    // default: "  "
} MarkdownSettings;

// Full settings (36 top-level fields + 7 nested structs)
typedef struct {
    // Model
    char *default_provider;
    char *default_model;
    char *default_thinking_level;   // "off"|"minimal"|"low"|"medium"|"high"|"xhigh"
    char **enabled_models;          // model patterns for Ctrl+P cycling
    int enabled_models_count;

    // Behavior
    char *transport;                // "sse"|"websocket"|"auto", default "sse"
    char *steering_mode;            // "all"|"one-at-a-time"
    char *follow_up_mode;           // "all"|"one-at-a-time"
    char *double_escape_action;     // "fork"|"tree"|"none", default "tree"
    char *tree_filter_mode;         // "default"|"no-tools"|"user-only"|"labeled-only"|"all"

    // UI
    char *theme;
    bool hide_thinking_block;
    bool quiet_startup;
    bool collapse_changelog;
    bool show_hardware_cursor;
    int editor_padding_x;           // default: 0
    int autocomplete_max_visible;   // default: 5

    // Shell
    char *shell_path;               // custom shell (e.g., Cygwin)
    char *shell_command_prefix;     // prepended to every bash command
    char **npm_command;             // argv-style npm override
    int npm_command_count;

    // Nested settings
    CompactionSettings compaction;
    BranchSummarySettings branch_summary;
    RetrySettings retry;
    TerminalSettings terminal;
    ImageSettings images;
    ThinkingBudgetsSettings thinking_budgets;
    MarkdownSettings markdown;

    // Resources
    cJSON *packages;                // array of package sources (string or object)
    char **extensions;              // local extension paths
    int extensions_count;
    char **skills;                  // local skill paths
    int skills_count;
    char **prompts;                 // local prompt template paths
    int prompts_count;
    char **themes_paths;            // local theme paths
    int themes_paths_count;
    bool enable_skill_commands;     // default: true

    // Telemetry & versioning
    char *last_changelog_version;
    bool enable_install_telemetry;  // default: true

    // Session
    char *session_dir;              // custom session storage directory
} Settings;

// Three-layer manager
typedef struct {
    Settings global;                // ~/.pi/agent/settings.json
    Settings project;               // .pi/settings.json
    Settings cli;                   // command-line overrides
    Settings merged;                // computed deep merge
    char *global_path;
    char *project_path;
} SettingsManager;

// Deep merge: project overrides global, CLI overrides both.
// Nested objects merge recursively (e.g., compaction.enabled from CLI
// doesn't clobber compaction.reserveTokens from project).
SettingsManager *settings_create(const char *global_path, const char *project_path);
cJSON *settings_get(SettingsManager *sm, const char *key);
int    settings_set(SettingsManager *sm, const char *layer, const char *key, cJSON *value);
int    settings_flush(SettingsManager *sm);
```

### Model Registry (model_registry.c)

```c
typedef struct {
    Model *builtin_models;
    int builtin_count;
    Model *custom_models;       // from models.json
    int custom_count;
    // auth resolution per provider
} ModelRegistry;

Model *registry_resolve_model(ModelRegistry *r, const char *pattern);
Model *registry_get_models(ModelRegistry *r, const char *provider, int *count);
int    registry_get_api_key(ModelRegistry *r, const char *provider, char **key);
```

### Built-in Tools

#### bash.c — Shell Execution
- Spawn via `fork()`/`exec()` (POSIX) or `CreateProcess` (Windows)
- Real-time stdout/stderr streaming via pipes
- Output sanitization: strip ANSI, replace non-UTF8
- Rolling buffer (1MB), temp file for full output
- Truncation for display (512KB/10K lines)
- AbortSignal → `kill(pid, SIGTERM)` then `SIGKILL`
- Working directory enforcement

#### read.c — File Reading
- Text files with line numbers (`cat -n` style)
- Binary detection (NUL bytes in first 8KB)
- Image files → base64 encode, return as ImageContent
- PDF support (via external tool or library)
- Line offset/limit for large files

#### write.c — File Writing
- Atomic write via temp file + rename
- Create parent directories
- File mutation queue (serialize concurrent writes)

#### edit.c — File Editing
- Search/replace mode: find `old_string`, replace with `new_string`
- Uniqueness check (old_string must be unique in file)
- `replace_all` flag for global replacement
- Diff mode: unified diff output for review

#### grep.c — Search
- Wrapper around `rg` (ripgrep)
- Auto-download rg binary if missing
- Pattern matching, file type filtering
- Context lines, max results

#### find.c — File Discovery
- Wrapper around `fd`
- Auto-download fd binary if missing
- Glob patterns, type filtering, depth limits

#### ls.c — Directory Listing
- `opendir()`/`readdir()` with stat info
- Sorted output, file type indicators

### Skills System (skills.c)

```c
typedef struct {
    char *name;
    char *description;
    char *content;              // full markdown
    char *path;                 // source file
    bool disable_model_invocation;
} Skill;

// Discovery: walk ~/.pi/agent/skills/, .pi/skills/, .agents/skills/
// Parse SKILL.md frontmatter
// Validate: name 1-64 chars, lowercase a-z/0-9/hyphens
// Format as XML for system prompt
Skill *skills_discover(const char **paths, int path_count, int *count);
char  *skills_format_xml(Skill *skills, int count);
```

### Prompt Templates (prompts.c)

```c
typedef struct {
    char *name;                 // filename without .md
    char *description;
    char *argument_hint;
    char *content;              // template body
    char *path;
} PromptTemplate;

// Argument substitution: $1, $2, $@, ${@:N}, ${@:N:L}
// Bash-style quoting for arguments
PromptTemplate *prompts_discover(const char **paths, int path_count, int *count);
char *prompts_expand(PromptTemplate *pt, const char **args, int arg_count);
```

### Theme System (themes.c)

```c
typedef struct {
    char *name;
    struct { char *key; char *value; } vars[32];   // max 32 variables
    int var_count;
    struct { char *token; char *color; } colors[64]; // 51 required + headroom
    int color_count;
} Theme;

// Color formats: "#ff0000" (hex), 242 (xterm 256), "primary" (var ref), "" (default)
// Hot reload via file watching
Theme *theme_load(const char *path);
int    theme_resolve_color(Theme *t, const char *token, int *fg_code);
```

### Package Manager (packages.c)

```c
typedef enum {
    PKG_NPM,
    PKG_GIT,
    PKG_LOCAL,
} PackageSource;

typedef struct {
    PackageSource source;
    char *specifier;            // "npm:@foo/bar@1.2.3" or "git:github.com/..."
    char *local_path;           // resolved install path
    char **extensions;          // filter patterns
    char **skills;
    char **prompts;
    char **themes;
} Package;

int package_install(const char *specifier, bool local);
int package_remove(const char *specifier, bool local);
int package_update(bool self, bool extensions);
Package *package_list(int *count);
```

**Note**: npm/git package installation requires shelling out to `npm`/`git` commands, same as TS version.

### Compaction (compaction.c)

When context exceeds token budget:
1. Calculate token count (simple tokenizer or external)
2. Collect old entries, keep recent
3. Ask LLM to summarize
4. Create compaction entry with summary
5. Inject as synthetic user message

### Token Estimation (tokens.c)

Accurate token counting is critical for compaction and context management.

```c
typedef enum {
    TOKENIZER_CHARS_DIV_4,      // fast heuristic: strlen/4
    TOKENIZER_CL100K,           // OpenAI cl100k_base (if available)
    TOKENIZER_PROVIDER_USAGE,   // use provider-reported usage from last response
} TokenizerMode;

// Estimation strategy:
// 1. Primary: chars/4 heuristic (matches original TS behavior, good enough for decisions)
// 2. Calibrate: after each LLM response, compare estimate vs provider-reported usage
// 3. Adjust: maintain per-model correction factor (e.g., cl100k ~= chars/3.5)
// 4. Images: hardcode 1200 tokens per image (matches original)
// 5. Tool definitions: count JSON schema chars/4
//
// No external tokenizer dependency needed. The chars/4 heuristic is what the
// original TS uses and it works well enough for compaction decisions.

int tokens_estimate_message(const Message *msg, TokenizerMode mode);
int tokens_estimate_messages(const Message *msgs, int count, TokenizerMode mode);
int tokens_estimate_image(void);  // returns 1200
```

### Slash Commands (slash_commands.c)

Built-in commands + extension-registered commands:

```c
typedef struct {
    char *name;
    char *description;
    char **(*get_completions)(const char *prefix, int *count);
    int (*handler)(const char **args, int argc, void *ctx);
    void *ctx;
    bool builtin;
} SlashCommand;

// Built-in commands (21, matching original TS):
// /settings, /model, /scoped-models, /export, /import, /share, /copy,
// /name, /session, /changelog, /hotkeys, /fork, /clone, /tree,
// /login, /logout, /new, /compact, /resume, /reload, /quit
// Extensions add more via pi->register_command()

SlashCommand *slash_commands_builtin(int *count);
int slash_command_dispatch(const char *input, void *session_ctx);
```

### Output Guard (output_guard.c)

**NEW in Pi-C** — not a port. The TS `output-guard.ts` is just stdout→stderr
redirection (75 lines). Pi-C adds proactive cost/token/tool-call limiting:

```c
typedef struct {
    int max_tokens_per_response; // default: model's maxTokens
    int max_tool_calls_per_turn; // default: 50
    double max_cost_per_session; // default: unlimited
    double accumulated_cost;
} OutputGuard;

int output_guard_check(OutputGuard *g, const Message *msg);  // 0=ok, 1=limit hit
```

### File Mutation Queue (mutation_queue.c)

Serialize concurrent file writes to prevent corruption:

```c
typedef struct {
    pthread_mutex_t lock;
    struct { char *path; int64_t queued_at; } *pending;
    int count;
} MutationQueue;

int mutation_queue_acquire(MutationQueue *q, const char *path, int timeout_ms);
void mutation_queue_release(MutationQueue *q, const char *path);
```

### Session CWD Tracking (session_cwd.c)

Track and enforce working directory per session:

```c
typedef struct {
    char *initial_cwd;      // cwd when session started
    char *effective_cwd;    // current effective cwd (may change on fork)
    char *git_root;         // nearest git root (for tool path sandboxing)
} SessionCwd;

SessionCwd *session_cwd_create(const char *cwd);
bool session_cwd_is_allowed(SessionCwd *cwd, const char *path);  // sandbox check
```

### Path Sandboxing (path_sandbox.c)

Tools validate paths to prevent directory escape:

```c
// Resolve and validate a path is within the allowed directory
// Returns NULL if path escapes sandbox (symlink tricks, ../ traversal, etc.)
char *path_sandbox_resolve(const char *base_dir, const char *requested_path);
```

### Signal Handling (signals.c)

```c
// Install signal handlers early in main()
void signals_init(void);

// SIGINT  → abort current LLM stream, checkpoint workflow if running
// SIGTERM → graceful shutdown: restore terminal, flush sessions, checkpoint
// SIGWINCH → trigger TUI resize
// SIGPIPE → ignore (broken HTTP connections)
```

### Session Migrations (migrations.c)

```c
// Migrate session files from older versions
// v1→v2: add timestamps, v2→v3: add entry types
int session_migrate(const char *path, int from_version, int to_version);
```

### HTML Export (export.c)

```c
// Export session to standalone HTML file
int session_export_html(Session *s, const char *output_path);

// Export session as shareable link (if configured)
int session_share(Session *s, char **url);
```

### System Prompt Builder (system_prompt.c)

Assemble from:
1. Base instructions
2. Tool snippets (name + description per tool)
3. Usage guidelines per tool
4. Context files (CLAUDE.md, AGENTS.md) — walk cwd ancestors
5. Skills XML
6. Extension `appendSystemPrompt` contributions
7. Metadata (date, cwd)

### Provider Registration Types

```c
// OAuth callback signatures for extension-registered providers
typedef struct {
    int (*login)(void *callbacks_ctx);
    int (*refresh_token)(const char *credentials, char **new_token);
    int (*get_api_key)(const char *credentials, char **api_key);
    int (*modify_models)(Model *models, int count, Model **out, int *out_count);  // optional
} OAuthConfig;

// Full provider registration (used by register_provider in PiExtensionAPI)
typedef struct {
    char *base_url;
    char *api;                  // "anthropic-messages", "openai-completions", etc.
    char *api_key_env;          // env var name for API key
    char *auth_header;          // "x-api-key" or "Authorization: Bearer"
    Model *models;
    int model_count;
    OAuthConfig *oauth;         // NULL if no OAuth
    ProviderStreamSimpleFn stream_simple;  // optional custom stream handler
} ProviderRegistration;
```

### Auth Storage (auth.c)

```c
typedef struct {
    char *provider;
    enum { AUTH_API_KEY, AUTH_OAUTH } type;
    char *value;                // key or token
    int64_t expires_at;         // for OAuth
    char *refresh_token;
} AuthEntry;

// Storage: ~/.pi/agent/auth.json
// Env var fallback: ANTHROPIC_API_KEY, OPENAI_API_KEY, etc.
// Command execution: "!command" syntax
```

### Modes

#### Interactive Mode (modes/interactive.c)
- Full TUI via pi-tui library
- Multi-line editor, markdown rendering
- Real-time streaming display
- Model selector overlay
- Session tree browser
- Keybindings (71 in original)

#### Print Mode (modes/print.c)
- `pi -p "prompt"` → stdout
- Text output (final response) or JSON (event stream)
- For scripting, CI/CD, piping

#### RPC Mode (modes/rpc.c)
- JSONL over stdio
- Bidirectional: server sends events, client sends commands
- Commands: prompt, interrupt, set_model, navigate, export, get_state
- For editor integrations (VS Code, JetBrains)

### CLI Argument Parsing (main.c)

```
pi [options] [initial-prompt]

Model:     --model, --provider, --models, --thinking
Session:   --session, --continue, --resume, --fork, --no-session
Tools:     --tools, --no-tools, --no-builtin-tools
Resources: --extensions, --skills, --prompts, --themes
Mode:      --mode (json|rpc), -p/--print
Output:    --export, --share, --import
```

Use `getopt_long()` or hand-rolled parser.

---

## 7. pi-tui: Terminal UI

### Differential Rendering Engine (tui.c)

Core algorithm:
1. Components render to string arrays via `render(width)`
2. Compare `previous_lines` with `new_lines`
3. Find `first_changed` and `last_changed` indices
4. Only redraw changed lines
5. Use synchronized output (`CSI ?2026h/l`) for flicker-free updates
6. 16ms minimum between renders (throttling)

```c
typedef struct Component Component;
struct Component {
    char **(*render)(Component *self, int width, int *line_count);
    void (*handle_input)(Component *self, const char *data);
    void (*invalidate)(Component *self);
    bool wants_key_release;
    bool focused;
    void *data;                 // component-specific state
};

typedef struct {
    Component **components;
    int component_count;
    char **previous_lines;
    int previous_line_count;
    // overlay system
    // terminal state
} TUI;

void tui_render(TUI *tui);
void tui_add_overlay(TUI *tui, Component *comp, OverlayOptions *opts);
```

### Terminal Abstraction (terminal.c)

```c
void terminal_enter_raw_mode(void);
void terminal_exit_raw_mode(void);
void terminal_get_size(int *cols, int *rows);
void terminal_hide_cursor(void);
void terminal_show_cursor(void);
void terminal_move_cursor(int row, int col);
void terminal_clear_line(void);
void terminal_write(const char *data, size_t len);

// Kitty keyboard protocol
void terminal_enable_kitty_keyboard(void);
void terminal_disable_kitty_keyboard(void);

// Bracketed paste
void terminal_enable_bracketed_paste(void);
void terminal_disable_bracketed_paste(void);
```

### Keyboard Input (keys.c)

State machine for parsing raw terminal input:
- Legacy escape sequences
- Kitty keyboard protocol (CSI u)
- xterm modifyOtherKeys
- Bracketed paste accumulation
- Mouse events (SGR format)

```c
typedef struct {
    char *id;                   // "enter", "ctrl+c", "alt+f", "shift+ctrl+d", etc.
    char *printable;            // printable character if any
    bool is_release;
} ParsedKey;

ParsedKey key_parse(const char *raw_input);
bool key_matches(const char *raw_input, const char *key_id);
```

### Widget System

All widgets implement the Component interface:

- **Text**: Word wrap with ANSI preservation
- **Markdown**: Full renderer with syntax highlighting (likely use a C markdown parser)
- **Input**: Single-line with emacs keys, kill ring, undo, horizontal scroll
- **Editor**: Multi-line (most complex widget)
- **SelectList**: Scrollable list with two-column layout
- **Box**: Container with padding/background
- **Image**: Kitty/iTerm2 inline image protocols
- **Loader**: Animated spinner

### ANSI & Unicode (ansi.c, unicode.c)

Critical for correct rendering:

```c
// Track SGR state across line breaks
typedef struct {
    bool bold, dim, italic, underline, blink, inverse, hidden, strikethrough;
    int fg_color;               // -1 = default, 0-255 = xterm, 256+ = RGB
    int bg_color;
    char *hyperlink_url;
} AnsiState;

void ansi_track(AnsiState *state, const char *sequence);
char *ansi_get_reset(AnsiState *state);
char *ansi_get_restore(AnsiState *state);

// Unicode width calculation
int unicode_display_width(const char *str);     // grapheme-aware
int unicode_char_width(uint32_t codepoint);     // East Asian width
```

### Overlay System (overlay.c)

Positioned layers composited over main content:
- 9 anchor points
- Absolute or percentage positioning
- Margins from terminal edges
- Focus stack management
- ANSI-aware line merging

---

## 8. Extension, Plugin & Workflow System

Pi-C goes far beyond the original Pi's hook-based extensions. The goal: **any workflow you can imagine should be buildable**. Sub-agents, plan mode, code review pipelines, multi-repo orchestration, approval gates, human-in-the-loop, parallel fan-out — all composable from primitives. Pi ships none of these as builtins. Extensions build all of them.

### 8.1 Extension Loading: Three Tiers

#### Tier 1: Shared Libraries (.so/.dylib/.dll) — Native Plugins
```c
// my_extension.c → my_extension.so
#include <pi.h>

PI_EXPORT void pi_extension_init(PiExtensionAPI *pi) {
    pi->on(pi, "session_start", my_session_handler, my_ctx);
    pi->register_tool(pi, &my_tool);
    pi->register_command(pi, "my-cmd", my_cmd_handler, my_ctx);
    pi->register_workflow(pi, &my_workflow);
}

// ABI version check — pi refuses to load mismatched plugins
PI_EXPORT int pi_abi_version(void) { return PI_ABI_VERSION; }
```

- `dlopen()`/`dlsym()` loading
- ABI version handshake prevents crashes from stale plugins
- Full access to all Pi APIs including workflow engine
- Best for: performance-critical extensions, custom providers, complex tools

#### Tier 2: Lua Scripts — Scripted Plugins
```lua
-- my_extension.lua
function init(pi)
    pi:on("tool_call", function(event, ctx)
        if event.tool_name == "bash" and event.input.command:match("rm%-rf") then
            return { block = true, reason = "rm -rf blocked by policy" }
        end
    end)

    pi:register_command("deploy", function(args, ctx)
        ctx:send_user_message("Run deployment checklist for " .. args[1])
    end)
end
```

- Embedded Lua 5.4 interpreter (~25KB)
- Hot-reloadable without restart
- Sandboxable (restrict fs/network access)
- Sandbox details:
  - **Blocked modules**: `os`, `io`, `loadfile`, `dofile`, `require` (unless whitelisted)
  - **Blocked functions**: `os.execute`, `os.remove`, `io.open`, `debug.*`
  - **Allowed modules**: `string`, `table`, `math`, `utf8`, `json` (Pi-provided)
  - **Isolation**: Separate `lua_State` per extension (no cross-extension state leaks)
  - **Memory limit**: 50MB per extension state (via `lua_setallocf` custom allocator)
  - **CPU limit**: 10,000 instruction count per hook call (via `lua_sethook`)
  - **C module loading**: Disabled (`package.cpath` cleared, `package.loadlib` removed)
  - Extensions opt into elevated permissions via manifest flag `sandbox: false`
- Best for: quick scripts, policy rules, simple automations

#### Tier 3: Declarative YAML/TOML Workflows — Zero-Code
```yaml
# .pi/workflows/code-review.yaml
name: code-review
description: "Multi-pass code review workflow"
trigger: /review
steps:
  - name: diff
    tool: bash
    args: { command: "git diff --staged" }
    save_as: staged_diff

  - name: security-scan
    prompt: |
      Review this diff for security issues. OWASP Top 10 focus.
      ```diff
      ${staged_diff}
      ```
    model: claude-sonnet-4-6
    save_as: security_report

  - name: quality-review
    prompt: |
      Review this diff for code quality, readability, correctness.
      ${staged_diff}
    model: claude-sonnet-4-6
    save_as: quality_report
    parallel_with: security-scan    # runs concurrently

  - name: synthesize
    prompt: |
      Combine these reviews into a single actionable report:
      ## Security: ${security_report}
      ## Quality: ${quality_report}
    model: claude-opus-4-7

  - name: approve
    gate: user_confirm
    message: "Apply suggested fixes? [y/n]"

  - name: apply
    condition: "approve.result == 'yes'"
    prompt: "Apply the fixes from the review report."
```

- Parsed at load time, compiled to workflow graph
- Variable interpolation (`${step_name}` → step output)
- Parallel steps, conditional branches, gates
- Best for: CI/CD pipelines, review workflows, team-shared automations

### 8.2 Core Workflow Engine

The heart of "crazy levels of custom workflows." This is **new** — the original Pi has nothing like it.

#### Workflow Primitives

```c
// A workflow is a directed graph of steps (may contain cycles via goto)
typedef struct Workflow Workflow;
typedef struct WorkflowStep WorkflowStep;
typedef struct WorkflowContext WorkflowContext;

// Step types — the atomic building blocks
typedef enum {
    STEP_PROMPT,        // Send prompt to LLM, capture response
    STEP_TOOL,          // Execute a tool directly
    STEP_BASH,          // Run shell command
    STEP_GATE,          // Pause for user/external approval
    STEP_CONDITION,     // Branch based on expression
    STEP_PARALLEL,      // Fan-out: run N steps concurrently
    STEP_JOIN,          // Fan-in: wait for parallel steps
    STEP_SUB_WORKFLOW,  // Invoke another workflow
    STEP_TRANSFORM,     // Transform data (jq-style or Lua snippet)
    STEP_LOOP,          // Iterate over collection
    STEP_RETRY,         // Retry on failure with backoff
    STEP_EMIT,          // Emit event to parent/bus
    STEP_WAIT_EVENT,    // Block until event received
    STEP_SPAWN_SESSION, // Create new agent session (sub-agent)
    STEP_HTTP,          // HTTP request (webhooks, APIs)
    STEP_CHECKPOINT,    // Save state for resumption
} StepType;

struct WorkflowStep {
    char *name;                 // unique within workflow
    StepType type;
    cJSON *config;              // type-specific config

    // Dataflow
    char **inputs;              // names of steps whose output we consume
    int input_count;
    char *save_as;              // variable name for output

    // Control flow — branching
    char *condition;            // expression: "step_name.status == 'success'"
    char *then_step;            // step name to jump to if condition true
    char *else_step;            // step name to jump to if condition false
    char *goto_step;            // unconditional jump (creates cycles — bounded by max_iterations)
    int max_iterations;         // bound for goto cycles, default 10, prevents infinite loops

    // Control flow — ordering
    char **depends_on;          // explicit ordering
    int depends_count;
    char *parallel_with;        // shorthand: run in parallel with named step (sugar for STEP_PARALLEL)

    // Control flow — error handling
    char *on_success;           // expression to eval on success (e.g., "set(state, 'testing')")
    char *on_failure;           // expression to eval on failure
    int max_retries;
    int retry_delay_ms;
    int timeout_ms;

    // For STEP_PARALLEL
    WorkflowStep **parallel_steps;
    int parallel_count;

    // For STEP_LOOP
    char *loop_over;            // expression yielding JSON array
    WorkflowStep *loop_body;

    // For STEP_HTTP
    struct {
        char *method;           // GET, POST, PUT, DELETE
        char *url;              // supports ${var} interpolation
        cJSON *headers;
        char *body;             // supports ${var} interpolation
        int timeout_ms;
        int max_retries;
        char *response_path;    // jq-style path to extract from response
    } http;
};

struct Workflow {
    char *name;
    char *description;
    char *trigger;              // slash command or event
    WorkflowStep *steps;
    int step_count;
    cJSON *defaults;            // default variable values
};

// Workflow execution context — carries state through execution
struct WorkflowContext {
    Workflow *workflow;
    cJSON *variables;           // step outputs + user-provided vars
    cJSON *metadata;            // timing, status per step
    AgentState *agent;          // access to agent for STEP_PROMPT
    PiExtensionAPI *pi;         // access to pi APIs
    void (*on_step_complete)(WorkflowContext *ctx, WorkflowStep *step, cJSON *result);
    void (*on_gate_request)(WorkflowContext *ctx, WorkflowStep *step, const char *message);
    bool aborted;
    char *checkpoint_path;      // for resumption
};

// Memory model for workflow contexts:
// - Workflow definitions (steps, config): arena-allocated, freed when workflow unloaded
// - WorkflowContext: heap-allocated (malloc/free), lives for duration of execution
// - Variables (step outputs): heap-allocated cJSON, freed individually when overwritten
// - Loop accumulations: tracked with size limit (default 100MB), error if exceeded
// - Sub-agent sessions: independent heap allocations, freed on session end
// - Checkpoints: serialize to JSON file, context rebuilt on resume
//
// NOT arena-allocated because workflows can run for hours/days (gates with 24h timeout).
// Arena is used for short-lived parsing/transformation within a single step.

// Public API
Workflow *workflow_parse_yaml(const char *path);
Workflow *workflow_parse_json(const char *path);
int workflow_validate(Workflow *wf, char **error);
int workflow_execute(Workflow *wf, WorkflowContext *ctx);
int workflow_resume(const char *checkpoint_path, WorkflowContext *ctx);
void workflow_abort(WorkflowContext *ctx);
void workflow_context_free(WorkflowContext *ctx);  // frees all heap allocations
```

#### Variable System & Expressions

Steps communicate via variables. Each step output is stored as `${step_name}`:

```c
// Variable resolution
// ${step_name}           → full output (string)
// ${step_name.field}     → JSON path into structured output
// ${step_name.status}    → "success" | "error" | "skipped" | "pending"
// ${step_name.duration}  → execution time in ms
// ${env.VAR_NAME}        → environment variable
// ${input.N}             → workflow invocation argument
// ${loop.item}           → current loop iteration item
// ${loop.index}          → current loop iteration index

char *workflow_resolve_var(WorkflowContext *ctx, const char *expr);
```

#### Condition Expressions — Boolean DSL

Simple boolean expressions for `condition`, `then`/`else` guards:

```
step_name.status == 'success'
step_name.output contains 'CRITICAL'
step_name.duration > 5000
env.CI == 'true'
input.1 == '--force'
NOT step_name.output contains 'error'
step_a.status == 'success' AND step_b.status == 'success'
```

Grammar:
```
expr     := term ((AND | OR) term)*
term     := NOT? atom
atom     := ref OP value | ref 'contains' string | '(' expr ')'
ref      := IDENT ('.' IDENT)* ('[' NUMBER ']')*
OP       := '==' | '!=' | '>' | '<' | '>=' | '<='
value    := string | number | 'true' | 'false' | 'null'
string   := '\'' [^']* '\''
```

Recursive descent parser. ~300 lines of C. No eval(), no injection risk — only reads variables, never mutates.

#### Transform Expressions — Mini-Language for STEP_TRANSFORM and on_success/on_failure

Transform expressions DO mutate state. They use a restricted Lua subset (NOT a custom DSL — leverage the embedded Lua interpreter):

```lua
-- STEP_TRANSFORM examples (executed as Lua with Pi bindings):
set("state", "building")                              -- set variable
append("reviews", { file = loop.item, review = file_review })  -- append to array
result = json_decode(step_output)                       -- parse JSON from LLM output
filtered = filter(items, function(x) return x.severity == "CRITICAL" end)
```

Why Lua instead of custom DSL:
- Already embedded for Tier 2 extensions
- Well-understood semantics
- Sandboxable (same sandbox as Lua extensions)
- Users already know it (or can learn in 10 minutes)
- Avoids inventing yet another expression language

Transform steps run in a sandboxed Lua state with:
- Read/write access to workflow variables
- `set(key, value)`, `get(key)`, `append(key, value)` helpers
- `json_decode(str)`, `json_encode(obj)` for LLM output parsing
- `filter(arr, fn)`, `map(arr, fn)`, `reduce(arr, fn, init)` collection ops
- NO filesystem, network, or os access
- Memory limit: 10MB per transform
- CPU limit: 100ms per transform

The `on_success`/`on_failure` fields also evaluate as Lua transform expressions.

#### Workflow Graph Execution Model

The graph is NOT a DAG — `goto` creates cycles. The executor handles this:

```c
typedef struct {
    WorkflowStep *steps;
    int step_count;
    int *adjacency;             // adjacency matrix (step_count x step_count)
    int *cycle_members;         // bitset: which steps participate in cycles
    int *iteration_counts;      // per-step: how many times executed
} WorkflowGraph;

// Execution algorithm:
// 1. Build graph from steps (resolve goto/then/else/depends_on/parallel_with)
// 2. Detect cycles (Tarjan's SCC)
// 3. Mark cycle members
// 4. Execute in topological order for acyclic portions
// 5. For cyclic portions: follow edges, track iteration_counts
// 6. Abort if any step exceeds max_iterations (default 10)
// 7. parallel_with desugars to STEP_PARALLEL wrapper node during graph build

int workflow_graph_build(Workflow *wf, WorkflowGraph **out);
int workflow_graph_execute(WorkflowGraph *g, WorkflowContext *ctx);
```

#### Sub-Agent Spawning (STEP_SPAWN_SESSION)

Pi ships no built-in sub-agents. But the workflow engine makes them trivial:

```yaml
# .pi/workflows/architect.yaml
name: architect
description: "Multi-agent architecture review"
steps:
  - name: security-agent
    type: spawn_session
    config:
      model: claude-sonnet-4-6
      system_prompt: "You are a security specialist. Review only for vulnerabilities."
      prompt: "Review: ${input.1}"
      tools: [read, grep, find]
    save_as: security_review

  - name: perf-agent
    type: spawn_session
    config:
      model: claude-sonnet-4-6
      system_prompt: "You are a performance specialist."
      prompt: "Review: ${input.1}"
      tools: [read, grep, find, bash]
    save_as: perf_review
    parallel_with: security-agent

  - name: synthesizer
    type: prompt
    prompt: |
      Synthesize these specialist reviews:
      Security: ${security_review}
      Performance: ${perf_review}
    model: claude-opus-4-7
```

Each `spawn_session` creates an independent agent session:
- Own model, system prompt, tools, thinking level
- Own message history (isolated context)
- Runs in parallel by default
- Output captured as variable for downstream steps

```c
typedef struct {
    char *model_pattern;        // resolved via model registry
    char *system_prompt;
    char *prompt;
    char **tool_names;          // which tools this sub-agent gets
    int tool_count;
    int thinking_level;
    int max_turns;              // prevent runaway loops
    int timeout_ms;
    bool inherit_tools;         // copy parent's tool set
    bool inherit_context;       // include parent's message history
} SpawnSessionConfig;

// Spawns isolated session, runs to completion, returns output
int workflow_spawn_session(SpawnSessionConfig *config, AgentState *parent,
                          char **output, cJSON **structured_output);
```

#### Parallel Fan-Out / Fan-In

```yaml
steps:
  - name: fan-out
    type: parallel
    steps:
      - name: check-types
        tool: bash
        args: { command: "tsc --noEmit" }
      - name: check-lint
        tool: bash
        args: { command: "eslint ." }
      - name: check-tests
        tool: bash
        args: { command: "npm test" }

  - name: fan-in
    type: condition
    condition: "check-types.status == 'success' AND check-lint.status == 'success' AND check-tests.status == 'success'"
    then: proceed
    else: fix-issues

  - name: fix-issues
    type: prompt
    prompt: |
      These checks failed:
      Types: ${check-types}
      Lint: ${check-lint}
      Tests: ${check-tests}
      Fix all issues.
```

#### Gates: Human-in-the-Loop

```yaml
- name: review-gate
  type: gate
  gate: user_confirm          # built-in: prompt user y/n
  message: "Deploy to production?"

- name: approval-gate
  type: gate
  gate: webhook               # wait for external webhook
  config:
    url: "https://api.example.com/approvals/${workflow.id}"
    poll_interval: 30s
    timeout: 24h

- name: slack-gate
  type: gate
  gate: custom                # extension-provided gate
  config:
    handler: "slack_approval"
    channel: "#deployments"
    message: "Approve deploy? React with :+1:"
```

Gates pause workflow execution until resolved. State is checkpointed automatically.

```c
typedef enum {
    GATE_USER_CONFIRM,      // interactive y/n prompt
    GATE_USER_INPUT,        // interactive free-text input
    GATE_WEBHOOK,           // HTTP callback
    GATE_FILE_WATCH,        // wait for file to appear/change
    GATE_CUSTOM,            // extension-provided
} GateType;

typedef struct {
    GateType type;
    char *message;
    cJSON *config;
    int timeout_ms;         // 0 = no timeout
    char *result;           // set when gate resolves
    bool resolved;
} Gate;

// Extensions register custom gate handlers
typedef int (*GateHandler)(Gate *gate, WorkflowContext *ctx);
void pi_register_gate(PiExtensionAPI *pi, const char *name, GateHandler handler);
```

#### Loops & Iteration

```yaml
- name: review-files
  type: loop
  loop_over: "${changed-files.output}"    # expects JSON array
  body:
    - name: review-single
      type: prompt
      prompt: "Review ${loop.item} for issues."
      model: claude-haiku-4-5
      save_as: file_review

    - name: collect
      type: transform
      transform: "append(reviews, { file: loop.item, review: file_review })"

- name: summary
  type: prompt
  prompt: "Summarize all file reviews: ${reviews}"
```

#### State Machines (via STEP_CONDITION + STEP_WAIT_EVENT)

For complex stateful workflows:

```yaml
name: deploy-pipeline
steps:
  - name: state
    type: transform
    transform: "set(state, 'building')"

  - name: build
    type: bash
    args: { command: "make build" }
    on_success: { set: "state=testing" }
    on_failure: { set: "state=failed" }

  - name: test
    condition: "state == 'testing'"
    type: bash
    args: { command: "make test" }
    on_success: { set: "state=deploying" }
    on_failure: { set: "state=failed" }

  - name: deploy
    condition: "state == 'deploying'"
    type: bash
    args: { command: "make deploy" }

  - name: failed
    condition: "state == 'failed'"
    type: prompt
    prompt: "Build/test failed. Diagnose and fix: ${build} ${test}"
    goto: build    # retry from build step
```

#### Checkpointing & Resumption

Long-running workflows (hours, days) can checkpoint and resume:

```c
// Checkpoint: serialize workflow state to disk
int workflow_checkpoint(WorkflowContext *ctx, const char *path);

// Resume: reload state and continue from last checkpoint
int workflow_resume(const char *checkpoint_path, WorkflowContext *ctx);
```

Checkpoint contains:
- All variable values
- Step completion status
- Gate states
- Current position in graph
- Pending parallel jobs

Stored as JSON in `~/.pi/agent/workflows/checkpoints/<id>.json`.

#### Workflow Composition

Workflows can invoke other workflows:

```yaml
name: full-review
steps:
  - name: code-review
    type: sub_workflow
    workflow: code-review       # reference another workflow
    args: ["${input.1}"]

  - name: security-review
    type: sub_workflow
    workflow: security-scan
    parallel_with: code-review

  - name: combine
    type: prompt
    prompt: "Combine: ${code-review} ${security-review}"
```

### 8.3 Event Bus — Inter-Extension Communication

Extensions and workflows communicate via a typed event bus:

```c
// Publish/subscribe with topic filtering
typedef struct {
    char *topic;                // e.g., "workflow.step_complete", "deploy.started"
    char *source;               // publisher extension name
    cJSON *data;
    int64_t timestamp;
} BusEvent;

// API
void pi_bus_publish(PiExtensionAPI *pi, const char *topic, cJSON *data);

// Subscribe with glob pattern matching on topic
void pi_bus_subscribe(PiExtensionAPI *pi, const char *pattern,
                      void (*handler)(BusEvent *event, void *ctx), void *ctx);
// e.g., "workflow.*", "deploy.*", "*"

// Request/reply pattern
cJSON *pi_bus_request(PiExtensionAPI *pi, const char *topic, cJSON *data, int timeout_ms);
void pi_bus_reply_handler(PiExtensionAPI *pi, const char *topic,
                          cJSON *(*handler)(cJSON *request, void *ctx), void *ctx);
```

This enables:
- Extension A publishes "files.changed" → Extension B reacts
- Workflow step emits "review.complete" → Slack extension sends notification
- CI extension requests "test.status" → test-runner extension replies

### 8.4 Programmable Hooks — Beyond Simple Events

The original Pi has 28 lifecycle events. Pi-C keeps all of them **plus** adds:

#### Hook Chains with Priority & Short-Circuit

```c
typedef struct {
    int priority;               // lower = earlier, default 100
    bool (*handler)(const char *event, cJSON *data, cJSON **result, void *ctx);
    void *ctx;
    char *name;                 // for debugging
} HookEntry;

// Hooks are ordered by priority
// Return true = continue chain, false = short-circuit
// Modify *result to alter event data downstream
```

#### New Hook Points (beyond original Pi)

| Hook | When | Can Modify |
|------|------|-----------|
| `workflow_before_step` | Before each workflow step | Step config, skip step |
| `workflow_after_step` | After each workflow step | Step output, retry |
| `workflow_gate` | Gate evaluation | Approval result |
| `session_spawn` | Sub-agent creation | Model, tools, prompt |
| `bus_publish` | Event published to bus | Event data, suppress |
| `prompt_transform` | Before prompt sent to LLM | Full prompt text |
| `response_filter` | After LLM response received | Filter/modify response |
| `tool_discover` | Tool list being built | Add/remove/modify tools |
| `model_select` | Model being chosen | Override model choice |
| `context_window` | Context being assembled | Inject/remove messages |
| `output_format` | Output being rendered | Transform display |
| `error_recover` | Error occurred | Recovery strategy |

#### Policy Engine

Compose hooks into reusable policies:

```c
// policy.h
typedef struct {
    char *name;
    char *description;
    HookEntry *hooks;
    int hook_count;
} Policy;

// Built-in policy helpers
Policy *policy_no_destructive_bash(void);   // block rm -rf, etc.
Policy *policy_require_review(void);         // gate before commits
Policy *policy_cost_limit(double max_usd);  // abort if cost exceeds limit
Policy *policy_model_routing(cJSON *rules); // route prompts to different models
```

### 8.5 Custom Modes

Extensions can register entirely new modes beyond interactive/print/RPC:

```c
typedef struct {
    char *name;                 // e.g., "web", "daemon", "mcp"
    char *description;
    int (*start)(AgentState *state, PiExtensionAPI *pi, cJSON *config);
    int (*handle_input)(const char *input, void *ctx);
    void (*stop)(void *ctx);
} CustomMode;

void pi_register_mode(PiExtensionAPI *pi, CustomMode *mode);
```

Use cases:
- **MCP server mode** — expose Pi as Model Context Protocol server
- **Web mode** — HTTP API serving agent responses
- **Daemon mode** — background agent watching file changes
- **Batch mode** — process list of prompts from file
- **REPL mode** — custom interactive loop for domain-specific workflows

### 8.6 Programmable Context Window

Extensions control what goes into the LLM context:

```c
// Context assembler hooks
typedef struct {
    // Called when building context for LLM call
    // Return messages to inject, or NULL to pass through
    AgentMessage *(*assemble)(AgentMessage *current, int count,
                              int *out_count, void *ctx);
    // Priority for ordering (lower = earlier in context)
    int priority;
} ContextProvider;

void pi_register_context_provider(PiExtensionAPI *pi, ContextProvider *provider);
```

Examples:
- **RAG provider** — inject relevant docs based on conversation
- **Memory provider** — inject long-term memory from vector DB
- **Codebase context** — inject relevant file contents based on topic
- **History compressor** — custom compaction strategy
- **Token budget manager** — ensure critical context fits

### 8.7 Tool Composition

Build complex tools from simpler ones:

```c
// Composite tool: sequence of tool calls with data passing
typedef struct {
    char *name;
    struct {
        char *tool_name;
        cJSON *(*build_args)(cJSON *prev_result, cJSON *original_args);
    } *steps;
    int step_count;
} CompositeTool;

// Example: "find_and_read" = find files matching pattern, then read first match
// Example: "edit_and_verify" = edit file, then run tests, rollback if fail
```

### 8.8 Full PiExtensionAPI

Complete API surface available to extensions:

```c
typedef struct PiExtensionAPI {
    // === Identity ===
    int abi_version;
    char *pi_version;

    // === Lifecycle Hooks (30+ events) ===
    void (*on)(struct PiExtensionAPI *api, const char *event,
               bool (*handler)(const char *event, cJSON *data, cJSON **result, void *ctx),
               void *ctx);
    void (*on_priority)(struct PiExtensionAPI *api, const char *event, int priority,
                        bool (*handler)(const char *event, cJSON *data, cJSON **result, void *ctx),
                        void *ctx);

    // === Tool Registration ===
    void (*register_tool)(struct PiExtensionAPI *api, const Tool *tool);
    void (*unregister_tool)(struct PiExtensionAPI *api, const char *name);
    Tool *(*get_tool)(struct PiExtensionAPI *api, const char *name);
    Tool **(*list_tools)(struct PiExtensionAPI *api, int *count);

    // === Command Registration ===
    void (*register_command)(struct PiExtensionAPI *api, const char *name,
                            int (*handler)(const char **args, int argc, void *ctx), void *ctx);

    // === Provider Registration (with OAuth support) ===
    void (*register_provider)(struct PiExtensionAPI *api, const char *name,
                              const ProviderRegistration *config);
    void (*unregister_provider)(struct PiExtensionAPI *api, const char *name);

    // === Workflow Registration ===
    void (*register_workflow)(struct PiExtensionAPI *api, Workflow *wf);
    void (*register_gate)(struct PiExtensionAPI *api, const char *name, GateHandler handler);
    int  (*execute_workflow)(struct PiExtensionAPI *api, const char *name,
                            const char **args, int argc);

    // === Mode Registration ===
    void (*register_mode)(struct PiExtensionAPI *api, CustomMode *mode);

    // === Context Providers ===
    void (*register_context_provider)(struct PiExtensionAPI *api, ContextProvider *provider);

    // === Event Bus ===
    void (*bus_publish)(struct PiExtensionAPI *api, const char *topic, cJSON *data);
    void (*bus_subscribe)(struct PiExtensionAPI *api, const char *pattern,
                          void (*handler)(BusEvent *event, void *ctx), void *ctx);
    cJSON *(*bus_request)(struct PiExtensionAPI *api, const char *topic, cJSON *data, int timeout_ms);

    // === Agent Control ===
    void (*send_message)(struct PiExtensionAPI *api, AgentMessage *msg);
    void (*send_user_message)(struct PiExtensionAPI *api, const char *text,
                              const char *deliver_as);  // "prompt" | "steer" | "followUp"
    int  (*spawn_session)(struct PiExtensionAPI *api, SpawnSessionConfig *config,
                          char **output, cJSON **structured);
    void (*abort_session)(struct PiExtensionAPI *api);

    // === Session State ===
    void (*append_entry)(struct PiExtensionAPI *api, const char *type, cJSON *data);
    cJSON *(*get_entry)(struct PiExtensionAPI *api, const char *type);
    void (*set_session_name)(struct PiExtensionAPI *api, const char *name);
    void (*set_label)(struct PiExtensionAPI *api, const char *entry_id, const char *label);

    // === Settings ===
    cJSON *(*get_setting)(struct PiExtensionAPI *api, const char *key);
    void (*set_setting)(struct PiExtensionAPI *api, const char *key, cJSON *value);

    // === Model ===
    Model *(*get_current_model)(struct PiExtensionAPI *api);
    void (*set_model)(struct PiExtensionAPI *api, const char *pattern);
    Model **(*list_models)(struct PiExtensionAPI *api, int *count);

    // === UI (interactive mode only, NULL in other modes) ===
    struct {
        void (*set_widget)(struct PiExtensionAPI *api, const char *key,
                          char **lines, int line_count, const char *placement);
        void (*set_status)(struct PiExtensionAPI *api, const char *key, const char *text);
        void (*set_footer)(struct PiExtensionAPI *api,
                          Component *(*render)(void *tui, void *theme, cJSON *data));
        void (*set_header)(struct PiExtensionAPI *api,
                          Component *(*render)(void *tui, void *theme));
        void (*notify)(struct PiExtensionAPI *api, const char *message, const char *level);
    } *ui;

    // === Tools (dynamic management) ===
    Tool **(*get_active_tools)(struct PiExtensionAPI *api, int *count);
    void (*set_active_tools)(struct PiExtensionAPI *api, const char **names, int count);

    // === Thinking ===
    int (*get_thinking_level)(struct PiExtensionAPI *api);
    void (*set_thinking_level)(struct PiExtensionAPI *api, int level);

    // === Shell ===
    int (*exec)(struct PiExtensionAPI *api, const char *command, char **output,
                int *exit_code, int timeout_ms);

    // === Commands (introspection) ===
    char **(*get_commands)(struct PiExtensionAPI *api, int *count);

    // === Shortcuts (interactive mode) ===
    void (*register_shortcut)(struct PiExtensionAPI *api, const char *key_id,
                              const char *description,
                              void (*handler)(void *ctx), void *ctx);

    // === Message rendering ===
    void (*register_message_renderer)(struct PiExtensionAPI *api, const char *message_type,
                                      Component *(*render)(cJSON *data, void *theme));

    // === Utilities ===
    void (*log)(struct PiExtensionAPI *api, const char *level, const char *fmt, ...);
    char *(*resolve_path)(struct PiExtensionAPI *api, const char *relative);
    cJSON *(*read_json_file)(struct PiExtensionAPI *api, const char *path);
    int (*write_json_file)(struct PiExtensionAPI *api, const char *path, cJSON *data);
    char *(*get_session_name)(struct PiExtensionAPI *api);

    // === Extension state (persisted across sessions) ===
    cJSON *(*state_get)(struct PiExtensionAPI *api, const char *key);
    void (*state_set)(struct PiExtensionAPI *api, const char *key, cJSON *value);

} PiExtensionAPI;
```

### 8.9 Discovery & Loading

```
Discovery order (highest precedence first):
1. CLI: --extension path.so, --extension path.lua, --workflow path.yaml
2. Project: .pi/extensions/*.{so,lua}, .pi/workflows/*.yaml
3. Global: ~/.pi/agent/extensions/*.{so,lua}, ~/.pi/agent/workflows/*.yaml
4. Packages: pi.extensions / pi.workflows in package.json
5. Settings: extensions/workflows arrays in settings.json
```

#### Extension Dependencies

Extensions declare dependencies via a companion manifest or export:

```c
// .so extensions: export a metadata function
PI_EXPORT const char **pi_extension_depends(int *count) {
    static const char *deps[] = { "auth-provider", "slack-notifier" };
    *count = 2;
    return deps;
}
```

```lua
-- Lua extensions: return depends table from init
function depends()
    return { "auth-provider", "slack-notifier" }
end
```

```yaml
# YAML workflows: frontmatter
depends:
  - slack-notifier
  - approval-gate
```

Resolution:
- Build dependency graph
- Topological sort (Kahn's algorithm)
- Cycle detection → error with cycle path printed
- Missing dependency → warn + skip dependent extension (not crash)
- No version constraints (kept simple — use packages for versioning)

#### Loading Sequence

1. Discover all extensions + workflows from all paths
2. Parse dependency declarations
3. Build dependency graph, detect cycles
4. Topological sort
5. ABI version check for .so files
6. Load in dependency order: call `pi_extension_init()` / `init()` for each
7. Parse and validate YAML/JSON workflows
8. Register all with runner
9. Emit `resources_discover` for dynamic additions

### 8.10 Example Workflows — What People Can Build

#### Multi-Agent Code Review
```yaml
name: review
trigger: /review
steps:
  - name: diff
    tool: bash
    args: { command: "git diff HEAD~1" }
  - name: security
    type: spawn_session
    config: { model: sonnet, prompt: "Security review: ${diff}", tools: [read, grep] }
  - name: quality
    type: spawn_session
    config: { model: sonnet, prompt: "Quality review: ${diff}", tools: [read, grep] }
    parallel_with: security
  - name: tests
    type: spawn_session
    config: { model: haiku, prompt: "Are tests adequate? ${diff}", tools: [read, grep, find] }
    parallel_with: security
  - name: report
    type: prompt
    prompt: "Synthesize into final report: ${security} ${quality} ${tests}"
    model: opus
```

#### Plan Mode (Not Built In — Built by User)
```yaml
name: plan
trigger: /plan
steps:
  - name: analyze
    type: prompt
    prompt: |
      Analyze this task and create a detailed implementation plan.
      Task: ${input.1}
      Output as numbered steps with file paths.
    model: opus
    save_as: plan

  - name: approve
    type: gate
    gate: user_confirm
    message: "Approve this plan?\n${plan}"

  - name: execute
    condition: "approve.result == 'yes'"
    type: loop
    loop_over: "${plan.steps}"
    body:
      - name: step
        type: prompt
        prompt: "Execute step ${loop.index}: ${loop.item}"
      - name: verify
        type: gate
        gate: user_confirm
        message: "Step ${loop.index} complete. Continue?"
```

#### CI/CD Pipeline
```yaml
name: ci
trigger: /ci
steps:
  - name: check
    type: parallel
    steps:
      - { name: types, tool: bash, args: { command: "tsc --noEmit" } }
      - { name: lint, tool: bash, args: { command: "eslint ." } }
      - { name: test, tool: bash, args: { command: "npm test" } }
      - { name: build, tool: bash, args: { command: "npm run build" } }

  - name: all-pass
    type: condition
    condition: "types.status == 'success' AND lint.status == 'success' AND test.status == 'success' AND build.status == 'success'"
    then: deploy-gate
    else: fix

  - name: fix
    type: prompt
    prompt: "Fix failures: ${types} ${lint} ${test} ${build}"
    goto: check

  - name: deploy-gate
    type: gate
    gate: user_confirm
    message: "All checks pass. Deploy?"

  - name: deploy
    tool: bash
    args: { command: "make deploy" }
```

#### Self-Improving Agent
```yaml
name: self-improve
trigger: /improve
steps:
  - name: task
    type: prompt
    prompt: "${input.1}"
    save_as: attempt

  - name: critique
    type: spawn_session
    config:
      model: opus
      system_prompt: "You are a code critic. Find every flaw."
      prompt: "Critique this solution:\n${attempt}"
    save_as: critique

  - name: has-issues
    type: condition
    condition: "critique contains 'CRITICAL' OR critique contains 'BUG'"
    then: improve
    else: done

  - name: improve
    type: prompt
    prompt: "Improve based on critique:\n${critique}\n\nOriginal:\n${attempt}"
    save_as: attempt
    goto: critique
    max_iterations: 3

  - name: done
    type: emit
    topic: "task.complete"
    data: { result: "${attempt}" }
```

#### Auto-Triage Incoming Issues
```lua
-- .pi/extensions/auto-triage.lua
function init(pi)
    pi:register_command("triage", function(args, ctx)
        local issues = ctx:bash("gh issue list --state open --json number,title,body --limit 20")
        local parsed = json.decode(issues)

        for _, issue in ipairs(parsed) do
            -- Spawn sub-agent per issue for parallel triage
            ctx:spawn_session({
                model = "haiku",
                prompt = string.format(
                    "Triage issue #%d: %s\n\n%s\n\nClassify: bug/feature/question/invalid. Suggest priority: P0-P3. Suggest labels.",
                    issue.number, issue.title, issue.body
                ),
                tools = {"read", "grep", "find"},
                on_complete = function(output)
                    pi:bus_publish("triage.complete", {
                        issue = issue.number,
                        triage = output
                    })
                end
            })
        end
    end)
end
```

---

## 9. Build System & Dependencies

### C Dependencies

| Library | Purpose | Size | Required |
|---------|---------|------|----------|
| **cJSON** | JSON parsing/generation | ~2K lines, vendorable | Yes |
| **libcurl** | HTTP client, SSL | System lib or vendored | Yes |
| **libuv** | Async I/O, event loop, processes | ~30K lines | Yes |
| **pcre2** | Regex for grep tool | System lib | Optional (can use ripgrep binary) |
| **utf8proc** | Unicode normalization, East Asian width | ~15K lines | Yes |
| **lua** | Lua 5.4 interpreter for scripted extensions | ~25K lines, vendorable | Yes |
| **libyaml** | YAML parsing for declarative workflows | ~10K lines | Yes |
| **linenoise** | Line editing (alternative to custom input) | ~1K lines | Optional |

### Build Options

**Option A: Makefile** — simple, portable, explicit
```makefile
CC = cc
CFLAGS = -std=c11 -Wall -Wextra -O2
LDFLAGS = -lcurl -luv -lm -ldl -lpthread
# ...
```

**Option B: CMake** — better for cross-platform, dependency management

**Recommendation**: Start with Makefile, move to CMake if cross-platform becomes important.

### Vendoring Strategy

- **cJSON**: Vendor (2 files, MIT)
- **utf8proc**: Vendor (MIT, small)
- **lua**: Vendor (MIT, ~25K lines, single-source build)
- **libyaml**: Vendor (MIT, ~10K lines)
- **libuv**: System dependency or vendored submodule
- **libcurl**: System dependency (too large to vendor)
- **pcre2**: System dependency or skip (use rg binary)

---

## 10. C vs Rust Decision Points

Track these during implementation. If 3+ hit "painful in C", pivot to Rust.

| Area | C Approach | Pain Level | Rust Alternative |
|------|-----------|------------|-----------------|
| JSON handling | cJSON + manual traversal | Medium-High | serde_json (zero pain) |
| HTTP + SSE streaming | libcurl + manual SSE parser | Medium | reqwest + eventsource |
| Async I/O | libuv callbacks | Medium | tokio async/await |
| String manipulation | Manual buffer management | High | String/&str (zero pain) |
| Memory safety | Manual (arena allocators help) | Medium | Automatic |
| Error handling | Return codes + goto cleanup | Medium | Result<T, E> |
| Unicode | utf8proc + manual grapheme logic | High | unicode-segmentation crate |
| TUI | Manual terminal codes | Low (natural in C) | crossterm/ratatui |
| Plugin system | dlopen | Low | dylib crate |
| Cross-platform | #ifdef hell | High | Cargo handles it |
| Testing | Custom framework or Unity | Medium | cargo test (built-in) |
| Workflow DAG execution | Manual graph walk + pthreads | Medium | tokio::spawn + join handles |
| YAML parsing | libyaml (C lib, decent) | Low | serde_yaml (zero pain) |
| Lua embedding | lua.h (designed for C) | Low | mlua crate (also easy) |
| Expression eval | Hand-rolled recursive descent | Low | Same in both languages |

### Pivot Criteria

Switch to Rust if:
1. JSON handling takes >30% of coding time
2. String bugs consume >2 days of debugging
3. Memory bugs (use-after-free, double-free) become recurring
4. Cross-platform support becomes blocking

### Rust Crate Equivalents

If pivoting:
- `serde` + `serde_json` → JSON
- `reqwest` → HTTP
- `tokio` → Async runtime
- `crossterm` + `ratatui` → TUI
- `clap` → CLI args
- `unicode-segmentation` + `unicode-width` → Unicode
- `libloading` → Plugin system

---

## 11. Implementation Phases

### Phase 0: Foundations (Week 1-2)
- [ ] Project skeleton, build system
- [ ] Arena allocator, dynamic string, dynamic array, hashmap
- [ ] cJSON integration, JSON path access wrapper
- [ ] HTTP + SSE parsing (libcurl wrapper)
- [ ] Basic tests

### Phase 1: pi-ai Core (Week 3-4)
- [ ] Core types (Message, ContentBlock, Tool, Model)
- [ ] Event stream (callback-based)
- [ ] Provider registry
- [ ] Anthropic provider (first provider)
- [ ] Message transformation
- [ ] Tool argument validation
- [ ] Model database (static or JSON-loaded)
- [ ] Test: stream a response from Anthropic API

### Phase 2: pi-agent Loop (Week 5)
- [ ] Agent state management
- [ ] Core loop algorithm
- [ ] Tool execution (sequential + parallel)
- [ ] Message queues (steering + follow-up)
- [ ] Before/after tool call hooks
- [ ] Test: agent loop with mock tools

### Phase 3: Minimal Harness — Print Mode (Week 6-7)
- [ ] CLI argument parsing
- [ ] Config/path resolution
- [ ] Settings manager (3-layer merge)
- [ ] Auth storage (env vars, auth.json)
- [ ] Model registry (built-in + custom)
- [ ] System prompt builder
- [ ] Built-in tools: bash, read, write, edit, grep, find, ls
- [ ] Print mode: `pi -p "prompt"` works end-to-end
- [ ] **MILESTONE: First working pi-c binary**

### Phase 4: Session & Resources (Week 8-9)
- [ ] Session management (JSONL tree)
- [ ] Context building from session
- [ ] Branching, forking
- [ ] Compaction
- [ ] Skills loader
- [ ] Prompt template expansion
- [ ] Theme loading
- [ ] Package manager (install/remove/update via npm/git)

### Phase 5: Extension System — Three Tiers (Week 10-12)
- [ ] Shared library loading (dlopen, ABI version check)
- [ ] PiExtensionAPI — core surface (hooks, tools, commands, providers)
- [ ] Hook chains with priority and short-circuit
- [ ] Event bus (publish/subscribe/request-reply)
- [ ] Lua 5.4 embedding + sandboxed Pi API bindings
- [ ] YAML workflow parser (libyaml)
- [ ] Extension/workflow discovery from all paths
- [ ] Extension state persistence
- [ ] Custom mode registration
- [ ] Context provider registration
- [ ] Test: .so extension + Lua extension + YAML workflow all loaded together

### Phase 6: Workflow Engine (Week 13-15)
- [ ] Workflow graph builder (YAML → step DAG)
- [ ] Workflow executor (sequential steps)
- [ ] Variable system & expression evaluator
- [ ] Parallel fan-out / fan-in (pthreads or libuv)
- [ ] Gate system (user_confirm, webhook, file_watch, custom)
- [ ] Sub-agent spawning (STEP_SPAWN_SESSION)
- [ ] Loop / iteration steps
- [ ] Conditional branching + goto
- [ ] Checkpointing & resumption
- [ ] Workflow composition (sub_workflow steps)
- [ ] Tool composition (composite tools)
- [ ] Retry with backoff
- [ ] Policy engine (composable hook sets)
- [ ] Test: multi-agent code review workflow end-to-end

### Phase 7: TUI (Week 16-18)
- [ ] Terminal abstraction (raw mode, escape sequences)
- [ ] Keyboard input parser (Kitty protocol support)
- [ ] Differential rendering engine
- [ ] Core widgets: Text, Input, Box, Loader
- [ ] Markdown renderer
- [ ] Editor widget
- [ ] SelectList widget
- [ ] Overlay system
- [ ] Image support (Kitty/iTerm2)
- [ ] Interactive mode: full TUI working
- [ ] Extension UI registration (widgets, status, footer, header)

### Phase 8: RPC & Additional Providers (Week 19-20)
- [ ] RPC mode (JSONL stdio)
- [ ] OpenAI Completions provider
- [ ] OpenAI Responses provider
- [ ] Google provider
- [ ] Bedrock provider
- [ ] Mistral provider

### Phase 9: Polish (Week 21+)
- [ ] SDK mode (embedding API via pi.h)
- [ ] Cross-platform testing (macOS, Linux, Windows)
- [ ] Performance profiling
- [ ] Documentation: extension authoring guide, workflow cookbook
- [ ] Package distribution (single static binary)
- [ ] Example extensions: plan-mode, code-review, auto-triage, ci-pipeline

---

## 12. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|------|--------|-----------|------------|
| JSON verbosity in C overwhelms development | High | High | Invest in good cJSON wrappers early. Pivot to Rust if >30% time on JSON |
| String handling bugs (buffer overflows, encoding) | High | Medium | Arena allocators, dynamic string type, fuzz testing |
| SSE parsing edge cases across providers | Medium | Medium | Port TS SSE tests, fuzz with real provider responses |
| Unicode width calculation errors (broken TUI) | Medium | High | Use utf8proc, port TS test cases for East Asian/emoji/grapheme |
| Provider API changes break compatibility | Medium | Medium | Match TS test suite, keep provider implementations modular |
| Extension system ABI breaks | Medium | Low | Stable versioned API, header-only types |
| libuv complexity for simple use cases | Medium | Medium | Consider simplifying to blocking I/O first, add async later |
| Cross-platform terminal differences | Medium | High | Focus Linux first, macOS second, Windows last |
| Scope creep from 87K lines of TS | High | High | Strict phasing, print mode MVP first |
| Workflow engine complexity exceeds C ergonomics | High | Medium | YAML parser + expression eval are bounded problems. If DAG execution gets hairy, Lua can absorb complexity |
| Lua sandboxing gaps | Medium | Low | Whitelist approach: only expose Pi API functions, block os/io/loadfile by default |
| Extension ABI breaks between versions | Medium | Medium | Version handshake, stable struct layout, opaque pointers for internal state |
| Workflow checkpoint corruption | Medium | Low | Atomic writes (temp + rename), JSON validation on resume |

---

## Summary

Pi is a beautifully layered system:

```
┌───────────────────────────────────────────────────────┐
│         Modes (interactive/print/rpc + custom modes)   │
├───────────────────────────────────────────────────────┤
│       Harness (sessions, settings, tools, auth)        │
├──────────┬──────────────┬─────────────────────────────┤
│   TUI    │  Workflow     │   Extensions (.so + Lua)    │
│          │  Engine       │   Skills / Prompts / Themes │
│          │  (DAG exec,   │   Event Bus (pub/sub)       │
│          │   gates,      │   Policy Engine             │
│          │   sub-agents, │   Context Providers         │
│          │   checkpoint) │   Custom Modes              │
├──────────┴──────────────┴─────────────────────────────┤
│           Agent Loop (stream → tools → loop)           │
├───────────────────────────────────────────────────────┤
│      AI (providers, streaming, models, transform)      │
├───────────────────────────────────────────────────────┤
│     Utilities (json, http, strings, fs, arena, lua)    │
└───────────────────────────────────────────────────────┘
```

Each layer has clear boundaries. Build bottom-up. Test each layer. Print mode is first milestone proving the stack. Workflow engine is the **differentiator** — makes Pi-C more powerful than the original.

Total estimated effort: **21-24 weeks** for one developer, assuming C stays viable. Rust pivot would reset ~2-3 weeks but likely accelerate later phases.

The single biggest advantage of C: **one static binary, zero runtime dependencies, instant startup.** Plus Lua embedded = scripted extensions without shipping a JS runtime. That's worth fighting for.
