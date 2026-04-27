# Pi-C Architecture Reference

Companion to [DESIGN.md](./DESIGN.md). DESIGN.md describes **what** we build and **why**.
This document describes **how the pieces fit together**: dependency graphs, data flow,
memory ownership, threading, error propagation, build order, API contracts, and testing.

An implementer should be able to read DESIGN.md + this file and start coding Phase 0
with zero ambiguity.

---

## Table of Contents

1. [Layer Dependency Graph](#1-layer-dependency-graph)
2. [Data Flow Diagrams](#2-data-flow-diagrams)
3. [Memory Ownership Model](#3-memory-ownership-model)
4. [Threading Model](#4-threading-model)
5. [Error Propagation Strategy](#5-error-propagation-strategy)
6. [Build Dependency Order](#6-build-dependency-order)
7. [API Contract Summary](#7-api-contract-summary)
8. [Configuration Cascade](#8-configuration-cascade)
9. [Extension ABI Contract](#9-extension-abi-contract)
10. [Testing Strategy](#10-testing-strategy)

---

## 1. Layer Dependency Graph

### Conceptual Layers (bottom-up)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ L5: Modes (interactive.c, print.c, rpc.c)                                  │
│     + SDK embedding (rig.h)                                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ L4: Harness (session.c, settings.c, tools/*.c, skills.c, prompts.c,       │
│     themes.c, packages.c, compaction.c, system_prompt.c, auth.c,          │
│     config.c, slash_commands.c, model_registry.c, signals.c)              │
├────────────────┬─────────────────┬──────────────────────────────────────────┤
│ L3a: TUI       │ L3b: Workflow   │ L3c: Extensions                         │
│ (tui.c,        │ (executor.c,    │ (loader.c, runner.c, bus.c, policy.c,   │
│  terminal.c,   │  parser.c,      │  lua_bindings.c)                        │
│  keys.c,       │  variables.c,   │                                         │
│  widgets/*.c,  │  gates.c,       │                                         │
│  overlay.c,    │  spawn.c,       │                                         │
│  ansi.c,       │  checkpoint.c)  │                                         │
│  unicode.c)    │                 │                                         │
├────────────────┴─────────────────┴──────────────────────────────────────────┤
│ L2: Agent Loop (agent.c, loop.c, proxy.c)                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ L1: AI (types.c, stream.c, registry.c, models.c, validation.c,            │
│     json_parse.c, transform.c, providers/*.c)                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ L0: Utilities (arena.c, str.c, vec.h, hashmap.c, json.c, http.c, fs.c,   │
│     process.c, log.h)                                                      │
└─────────────────────────────────────────────────────────────────────────────┘
```

### File-Level Compile-Time Dependencies

Arrows show `#include` direction. A --> B means A includes B's header.

```
main.c ──> harness/* ──> agent/* ──> ai/* ──> util/*
   │            │            │          │
   │            ├──> tui/*   │          ├──> deps/cJSON
   │            │     │      │          ├──> deps/libcurl (via http.h)
   │            │     └──> util/*       └──> deps/utf8proc (via types.h)
   │            │
   │            ├──> extensions/* ──> deps/lua
   │            │         │
   │            │         └──> harness/workflow/* ──> deps/libyaml
   │            │
   │            └──> util/*
   │
   └──> util/*
```

### Detailed Header Dependency Map

```
util/arena.h          : (standalone, no deps)
util/str.h            : arena.h
util/vec.h            : (standalone, macro-only header)
util/hashmap.h        : arena.h
util/json.h           : deps/cJSON/cJSON.h
util/http.h           : (standalone)
util/fs.h             : (standalone)
util/process.h        : (standalone)
util/log.h            : (standalone)

ai/types.h            : util/json.h (cJSON for tool args, details)
ai/stream.h           : ai/types.h
ai/registry.h         : ai/types.h, ai/stream.h
ai/models.h           : ai/types.h
ai/validation.h       : util/json.h
ai/json_parse.h       : util/json.h
ai/transform.h        : ai/types.h
ai/providers/*.c      : ai/types.h, ai/stream.h, util/http.h, util/json.h, util/str.h

agent/agent.h         : ai/types.h, ai/stream.h
agent/loop.h          : agent/agent.h
agent/proxy.c         : agent/agent.h, ai/types.h, util/http.h

harness/session.h     : ai/types.h, agent/agent.h
harness/settings.h    : util/json.h
harness/model_registry.h : ai/types.h, ai/models.h
harness/extensions/types.h : ai/types.h, agent/agent.h, harness/session.h
harness/extensions/loader.h : harness/extensions/types.h
harness/extensions/runner.h : harness/extensions/types.h
harness/extensions/bus.h    : util/json.h
harness/extensions/lua_bindings.h : harness/extensions/types.h, deps/lua/lua.h
harness/workflow/types.h    : util/json.h, agent/agent.h
harness/workflow/parser.h   : harness/workflow/types.h, deps/libyaml/yaml.h
harness/workflow/executor.h : harness/workflow/types.h
harness/tools/*.c     : ai/types.h, util/process.h, util/fs.h, util/str.h
harness/skills.c      : util/fs.h, util/str.h
harness/prompts.c     : util/str.h
harness/themes.c      : util/json.h, util/fs.h
harness/compaction.c  : harness/session.h, ai/types.h
harness/system_prompt.c : harness/skills.c, harness/settings.h, util/str.h
harness/auth.c        : util/json.h, util/fs.h
harness/config.c      : util/fs.h

tui/tui.h             : (standalone)
tui/terminal.h        : (standalone)
tui/keys.h            : (standalone)
tui/widgets/*.c       : tui/tui.h, tui/terminal.h, util/str.h
tui/overlay.c         : tui/tui.h
tui/ansi.c            : (standalone)
tui/unicode.c         : deps/utf8proc/utf8proc.h

harness/modes/interactive.c : tui/*, harness/*, agent/*
harness/modes/print.c       : harness/*, agent/*
harness/modes/rpc.c         : harness/*, agent/*, util/json.h

include/rig.h          : ai/types.h, agent/agent.h, harness/extensions/types.h
```

### Runtime-Only Dependencies (not compile-time)

- `providers/*.c` --> libcurl (HTTP requests at runtime)
- `harness/tools/bash.c` --> fork/exec (child processes)
- `harness/tools/grep.c` --> `rg` binary (external process)
- `harness/tools/find.c` --> `fd` binary (external process)
- `harness/extensions/loader.c` --> dlopen (shared library loading)
- `harness/extensions/lua_bindings.c` --> lua (embedded interpreter)
- `harness/workflow/parser.c` --> libyaml (YAML parsing)
- `tui/terminal.c` --> termios (POSIX terminal control)

---

## 2. Data Flow Diagrams

### 2.1 Interactive Mode: User Input to Output

```
User types in editor
       │
       v
  [TUI keys.c] ──parse raw input──> [Editor widget]
       │                                    │
       │                              text + images
       │                                    │
       v                                    v
  [interactive.c] ──── extension "input" event ────> [runner.c]
       │                    (can transform/handle)        │
       │ <──────── result: continue/transform/handled ────┘
       │
       v
  [interactive.c] ──── "before_agent_start" event ──> [runner.c]
       │                 (can inject msg, replace sysprompt)
       │
       v
  [agent-session.c]
       │
       ├── build system prompt ──> [system_prompt.c]
       │     reads: base instructions, tool snippets, context files,
       │            skills XML, extension appendSystemPrompt, cwd, date
       │
       ├── build context ──> [session.c: build_context()]
       │     walks JSONL tree from leaf to root, applies compaction
       │
       ├── wrap tools ──> [extensions/runner.c]
       │     builtin tools + extension tools, filtered by active list
       │
       v
  [Agent.prompt()] ──> [agent.c]
       │
       ├── emit AGENT_START
       │
       ├── OUTER LOOP:
       │   ├── drain steering queue
       │   │
       │   ├── INNER LOOP:
       │   │   ├── emit TURN_START
       │   │   ├── inject pending messages
       │   │   │
       │   │   ├── transformContext() ──> [extension "context" event]
       │   │   │     extensions can inject/remove messages
       │   │   │
       │   │   ├── convertToLlm() ──> [messages.c]
       │   │   │     AgentMessage[] -> Message[]
       │   │   │     custom messages -> user messages or filtered out
       │   │   │
       │   │   ├── getApiKey() ──> [auth.c] (resolve, possibly refresh OAuth)
       │   │   │
       │   │   ├── streamSimple() ──> [stream.c] ──> [registry.c]
       │   │   │         │
       │   │   │         v
       │   │   │   [providers/anthropic.c] (or appropriate provider)
       │   │   │         │
       │   │   │         ├── build request JSON
       │   │   │         ├── "before_provider_request" event
       │   │   │         ├── HTTP POST via [http.c] + libcurl
       │   │   │         ├── "after_provider_response" event
       │   │   │         ├── parse SSE stream ──> [http.c: sse_parser]
       │   │   │         ├── emit StreamEvents via callback
       │   │   │         │     START -> TEXT_DELTA* -> TOOLCALL_DELTA* -> DONE
       │   │   │         └── normalize to AssistantMessage
       │   │   │
       │   │   ├── emit MESSAGE_START, MESSAGE_UPDATE*, MESSAGE_END
       │   │   │         │
       │   │   │         └──> [interactive.c] renders streamed markdown
       │   │   │              via [tui/widgets/markdown.c]
       │   │   │
       │   │   ├── extract tool calls from assistant message
       │   │   │
       │   │   ├── for each tool call:
       │   │   │   ├── validate args ──> [validation.c]
       │   │   │   ├── "tool_call" event ──> [runner.c] (can block/modify)
       │   │   │   ├── emit TOOL_EXEC_START
       │   │   │   ├── execute ──> [tools/bash.c | read.c | etc.]
       │   │   │   ├── "tool_result" event ──> [runner.c] (can modify)
       │   │   │   ├── emit TOOL_EXEC_END
       │   │   │   └── create ToolResultMessage
       │   │   │
       │   │   ├── emit TURN_END
       │   │   └── drain steering queue -> pending
       │   │
       │   ├── drain follow-up queue
       │   └── if follow-ups, continue OUTER
       │
       └── emit AGENT_END
              │
              v
        [session.c: append entries to JSONL]
              │
              v
        [interactive.c: update TUI, show final state]
```

### 2.2 Print Mode: Prompt to stdout

```
CLI: rig -p "prompt"
       │
       v
  [main.c: parseArgs()]
       │
       ├── resolve model ──> [model_registry.c]
       ├── resolve auth ──> [auth.c]
       ├── load settings ──> [settings.c]
       ├── discover resources ──> [skills.c, prompts.c]
       ├── build system prompt ──> [system_prompt.c]
       ├── create tools ──> [tools/*.c]
       │
       v
  [print.c: run()]
       │
       ├── create Agent
       ├── agent.prompt(user_message)
       │     (same loop as interactive, minus TUI rendering)
       │
       ├── collect all events
       │     ├── text mode: accumulate final text response
       │     └── json mode: emit each AgentEvent as JSON line
       │
       └── write to stdout
```

### 2.3 RPC Mode: JSONL Protocol

```
Editor (VS Code) ──stdin──> [rpc.c]
                              │
                   JSONL commands:
                   { "type": "prompt", "text": "...", "id": "abc" }
                   { "type": "interrupt", "id": "def" }
                   { "type": "set_model", "model": "...", "id": "ghi" }
                              │
                              v
                   [rpc.c: command_dispatch()]
                              │
                   ┌──────────┼──────────┐
                   v          v          v
              prompt()   abort()   set_model()
                   │
                   v
              [AgentSession] (same as interactive)
                   │
                   ├── events emitted as JSONL on stdout
                   │   { "type": "message_update", ... }
                   │   { "type": "tool_execution_start", ... }
                   │   { "type": "response", "command": "prompt", "success": true }
                   │
                   └── extension UI requests:
                       { "type": "extension_ui_request", "requestId": "...",
                         "method": "select", "title": "...", "options": [...] }
                       client responds:
                       { "type": "extension_ui_response", "requestId": "...",
                         "result": "option1" }
```

### 2.4 SDK Mode: Programmatic Embedding

```
Host application
       │
       v
  #include <rig.h>
       │
       ├── rig_session_create(config) ──> [session.c, settings.c, auth.c]
       │
       ├── rig_session_prompt(session, "text")
       │     │
       │     └── [agent.c loop] ──> events via callback
       │           rig_subscribe(session, callback, userdata)
       │
       ├── rig_session_abort(session)
       │
       ├── rig_session_get_state(session) ──> AgentState snapshot
       │
       └── rig_session_destroy(session) ──> free all resources
```

---

## 3. Memory Ownership Model

### 3.1 Arena-Allocated (Short-Lived)

| Struct | Allocator | Owner | Lifetime | Notes |
|--------|-----------|-------|----------|-------|
| SSE parse buffers | Per-request arena | Provider stream fn | Single HTTP response | Reset after each response |
| JSON parse scratch | Per-parse arena | json_parse functions | Single parse call | Temp buffers for repair/streaming parse |
| Transform scratch | Per-transform arena | transform.c | Single transform call | Message normalization temporaries |
| Workflow step scratch | Per-step arena | executor.c | Single step execution | Variable resolution, expression eval |
| TUI render scratch | Per-render arena | tui.c | Single render cycle (16ms) | Line arrays, ANSI state tracking |
| Validation scratch | Per-validate arena | validation.c | Single validation call | Error message construction |

### 3.2 Heap-Allocated (Long-Lived, Individually Freed)

| Struct | Allocator | Owner | Lifetime | Free Trigger |
|--------|-----------|-------|----------|-------------|
| `Message` | malloc | AgentState.messages | Session-scoped | Session destroy or compaction |
| `AgentMessage` | malloc | AgentState.messages | Session-scoped | Session destroy or compaction |
| `ContentBlock` | malloc | Parent Message | Message lifetime | Freed with parent Message |
| `ContentBlock.text.text` | strdup | ContentBlock | ContentBlock lifetime | Freed with parent ContentBlock |
| `ContentBlock.tool_call.arguments` | cJSON_Duplicate | ContentBlock | ContentBlock lifetime | cJSON_Delete with parent |
| `AgentState` | malloc | Agent | Agent lifetime | agent_destroy() |
| `Session` | malloc | Harness/Mode | Session-scoped | session_destroy() or session switch |
| `SessionEntry` | malloc | Session.entries | Session-scoped | session_destroy() |
| `SessionEntry.data` | cJSON_Parse | SessionEntry | Entry lifetime | Freed with parent entry |
| `Model` | static or malloc | ModelRegistry | Global (static) or registry-scoped | Never (static) or registry_destroy() |
| `Tool` | malloc | Tool registry | Registration-scoped | unregister or session_destroy |
| `SettingsManager` | malloc | Harness | App lifetime | settings_destroy() |
| `SettingsManager.merged` | cJSON | SettingsManager | Until next merge | cJSON_Delete on recompute |
| `WorkflowContext` | malloc | Executor | Workflow execution | workflow_context_free() |
| `WorkflowContext.variables` | cJSON | WorkflowContext | Until overwritten | cJSON_Delete per key |
| `RigExtensionAPI` | malloc | ExtensionRunner | Extension lifetime | Extension unload |
| `lua_State` | lua_newstate | Extension loader | Extension lifetime | lua_close on unload |
| `Theme` | malloc | Theme registry | Until hot-reload | theme_destroy on reload |
| `Skill` | malloc | Skills registry | Session-scoped | Freed on reload |
| `PromptTemplate` | malloc | Prompts registry | Session-scoped | Freed on reload |
| `TUI` | malloc | Interactive mode | App lifetime | tui_destroy() |
| `Component` | malloc | TUI or overlay | Varies per component | Component-specific destroy |

### 3.3 Ownership Rules

1. **Producer allocates, consumer borrows.** Functions that produce data (parse, create) return owned pointers. Callers that receive data for read-only use do NOT free it.

2. **Message content is owned by the Message.** When a Message is freed, all its ContentBlocks and their inner strings/cJSON are freed.

3. **AgentState.messages owns all messages.** Messages pushed into the state are now owned by the state. The caller must NOT free them after pushing.

4. **Session entries own their data.** Each SessionEntry owns its `data` cJSON. When the session is destroyed, all entries and their data are freed.

5. **cJSON ownership follows cJSON conventions.** `cJSON_Parse` returns owned trees. `cJSON_GetObjectItem` returns borrowed pointers. Use `cJSON_Duplicate` for owned copies.

6. **Arena lifetime is explicit.** An arena is created at the start of a scope (function, request, render cycle) and destroyed at the end. All allocations from it become invalid simultaneously. Never store arena pointers in longer-lived structures.

7. **Tool results are owned by the caller.** `tool.execute()` allocates result content. The agent loop takes ownership and incorporates it into ToolResultMessages.

### 3.4 Cleanup Patterns

```c
// Pattern 1: Arena scope
void process_request(void) {
    Arena arena = arena_create(64 * 1024);  // 64KB
    // ... all temporary allocations from arena ...
    arena_destroy(&arena);  // everything freed at once
}

// Pattern 2: Goto cleanup
int session_load(const char *path, Session **out) {
    int rc = -1;
    Session *s = calloc(1, sizeof(Session));
    if (!s) return -1;

    s->entries = NULL;
    if (parse_jsonl(path, &s->entries, &s->entry_count) != 0) goto cleanup;
    if (migrate_entries(s) != 0) goto cleanup;

    *out = s;
    return 0;

cleanup:
    session_destroy(s);
    return rc;
}

// Pattern 3: Transfer ownership
void agent_state_push_message(AgentState *state, Message *msg) {
    // state takes ownership of msg
    vec_push(state->messages, msg);
    // caller must NOT free msg after this
}
```

---

## 4. Threading Model

### 4.1 Single-Threaded Components

| Component | Thread | Notes |
|-----------|--------|-------|
| Main event loop | Main thread | All TUI rendering, input handling, event dispatch |
| Agent loop logic | Main thread | State machine, event emission, message management |
| Session persistence | Main thread | JSONL append is synchronous (fast enough) |
| Settings manager | Main thread | File watching via main loop timer |
| Extension event dispatch | Main thread | Hooks run sequentially on main thread |
| Slash command handlers | Main thread | User-initiated, run in main thread |

### 4.2 Thread Pool (Parallel Tool Execution)

```
Main Thread                    Thread Pool (pthreads)
     │                              │
     ├── prepare all tool calls     │
     │   (sequential, main thread)  │
     │                              │
     ├── dispatch to pool ─────────>├── tool_exec_job[0]: bash
     │                              ├── tool_exec_job[1]: read
     │                              ├── tool_exec_job[2]: grep
     │                              │
     ├── wait for all ─────────────>│ (barrier or condition variable)
     │                              │
     ├── collect results            │
     │   (main thread)              │
     └── emit events                │
```

**Synchronization requirements:**
- `MutationQueue` (file writes): `pthread_mutex_t` protects concurrent write serialization
- Tool `onUpdate` callbacks: must be thread-safe (post to main thread queue)
- Agent abort signal: atomic flag checked by tools

### 4.3 Workflow Parallel Steps

```
Workflow Executor (main thread)
     │
     ├── detect parallel group
     │
     ├── spawn threads ────────────> Thread 1: step "security"
     │                               Thread 2: step "quality"
     │                               Thread 3: step "tests"
     │
     ├── each thread runs its own agent loop
     │   (independent AgentState, own HTTP connections)
     │
     ├── barrier wait ─────────────> all complete
     │
     ├── collect outputs into variables
     └── continue to next step
```

**Synchronization requirements:**
- Each parallel step gets its own `AgentState` (no sharing)
- Variable store: mutex-protected for concurrent `save_as` writes
- Gate resolution: condition variable for blocking wait

### 4.4 Sub-Agent Sessions

Each `spawn_session` creates a fully independent context:
- Own `AgentState` (messages, tools, model)
- Own HTTP connection (libcurl easy handle)
- Own arena for request scratch
- Shares: `ModelRegistry` (read-only), `AuthStorage` (thread-safe reads)

### 4.5 HTTP Streaming

```
libcurl write callback (called from curl_easy_perform, same thread)
     │
     v
  sse_parser_feed(chunk)
     │
     v
  on_event(type, data)  ──> provider parse logic
     │
     v
  StreamCallback(event, userdata)  ──> agent loop processes event
```

libcurl is used in blocking mode per-request. Each parallel tool or sub-agent
that makes HTTP calls does so on its own thread. No libuv event loop sharing
is needed for HTTP -- libcurl handles its own I/O.

### 4.6 TUI Rendering

Single-threaded, driven by main loop:

```
Input arrives (stdin read)
     │
     v
  key_parse() ──> component.handleInput()
     │
     v
  state change triggers invalidation
     │
     v
  render timer fires (16ms throttle)
     │
     v
  tui_render():
     ├── for each component: lines = component.render(width)
     ├── diff against previous_lines
     ├── write only changed lines (synchronized output)
     └── store as previous_lines
```

### 4.7 Thread Safety Summary

| Resource | Protection | Access Pattern |
|----------|-----------|----------------|
| AgentState.messages | Main thread only | Read/write from main thread; parallel tools get snapshots |
| Session JSONL file | Main thread only | Append-only, no concurrent writes |
| MutationQueue | pthread_mutex | Parallel tools serialize file writes |
| ModelRegistry | Read-only after init | Safe for concurrent reads |
| AuthStorage | Read + occasional refresh | Main thread refresh, read from any |
| WorkflowContext.variables | pthread_mutex | Parallel steps write concurrently |
| TUI state | Main thread only | Never accessed from other threads |
| Extension state | Main thread only | Hooks run sequentially |
| cJSON objects | NOT thread-safe | Never share across threads; duplicate if needed |
| Arena allocators | NOT thread-safe | Each thread gets its own arena |

---

## 5. Error Propagation Strategy

### 5.1 Layer-by-Layer Error Flow

```
Provider HTTP error
  └──> StreamEvent { type: EVENT_ERROR, error_message: "..." }
         └──> Agent loop receives error event
                └──> Creates AssistantMessage { stopReason: STOP_ERROR, error_message }
                       └──> Emits AGENT_EVENT_MESSAGE_END with error message
                              └──> Emits AGENT_EVENT_TURN_END
                                     └──> Emits AGENT_EVENT_AGENT_END
                                            └──> Session appends error message entry
                                                   └──> Mode displays error to user

Tool execution error
  └──> execute() returns non-zero, sets content to error text
         └──> ToolResultMessage { isError: true, content: [error text] }
                └──> Agent loop adds to context
                       └──> LLM sees error, may retry or report
                              └──> Normal flow continues

Extension hook error
  └──> Hook function returns non-zero or throws (Lua pcall)
         └──> runner.c logs error, skips this hook, continues chain
                └──> Other hooks in chain still execute
                       └──> Agent flow continues uninterrupted

Workflow step error
  └──> Step returns error status
         └──> on_failure expression evaluated (if configured)
                └──> If retries remaining: backoff, retry step
                       └──> If retries exhausted: check for error handler step
                              └──> If no handler: workflow aborts
                                     └──> Checkpoint saved before abort
                                            └──> User sees error in UI

Context overflow error
  └──> Provider returns context-too-large error
         └──> isContextOverflow() matches error pattern
                └──> Agent session triggers auto-compaction
                       └──> Compaction reduces context
                              └──> Automatic retry with reduced context

Session file I/O error
  └──> appendFileSync fails
         └──> Logged, session continues in-memory
                └──> Next successful write catches up

Settings parse error
  └──> JSON parse fails
         └──> Error queued in SettingsManager.errors[]
                └──> Drained and displayed as warnings at startup
                       └──> Defaults used for unparseable settings
```

### 5.2 Return Code Convention

All C functions use this convention:

```c
// 0 = success, negative = error
int function_name(args..., output_params...);

// Error codes (defined in util/errors.h)
#define RIG_OK          0
#define RIG_ERR        -1    // Generic error
#define RIG_ERR_ALLOC  -2    // Memory allocation failed
#define RIG_ERR_IO     -3    // File/network I/O error
#define RIG_ERR_PARSE  -4    // JSON/YAML/SSE parse error
#define RIG_ERR_AUTH   -5    // Authentication error
#define RIG_ERR_ABORT  -6    // Operation aborted by user
#define RIG_ERR_LIMIT  -7    // Token/cost/iteration limit exceeded
#define RIG_ERR_VALIDATE -8  // Schema validation error
#define RIG_ERR_NOT_FOUND -9 // Resource not found
```

### 5.3 Error Context

Functions that can fail carry an error context string:

```c
// Thread-local error message
void rig_set_error(const char *fmt, ...);
const char *rig_get_error(void);
void rig_clear_error(void);
```

### 5.4 Provider-Specific Error Handling

Each provider must handle:
1. **HTTP errors** (4xx, 5xx) -- extract error message from response body
2. **SSE parse errors** -- malformed events, incomplete JSON
3. **Rate limiting** (429) -- extract retry-after header, respect maxRetryDelayMs
4. **Context overflow** -- pattern-match error message, signal for compaction
5. **Network errors** -- connection refused, timeout, DNS failure
6. **Auth errors** (401, 403) -- surface as auth-specific error for UI guidance

---

## 6. Build Dependency Order

### Phase 0: Foundations (can all be built independently)

```
Build order     File                  Tests with
────────────────────────────────────────────────
1               util/arena.c          test_arena.c (standalone)
2               util/str.c            test_str.c (needs arena)
3               util/vec.h            test_vec.c (header-only, needs arena)
4               util/hashmap.c        test_hashmap.c (needs arena)
5               util/json.c           test_json.c (needs cJSON)
6               util/http.c           test_http.c (needs libcurl) -- can defer
7               util/fs.c             test_fs.c (standalone)
8               util/process.c        test_process.c (standalone)
```

### Phase 1: AI Core (sequential dependencies)

```
Build order     File                  Depends on         Tests with
──────────────────────────────────────────────────────────────────
9               ai/types.c            util/json          test_types.c (construct/free messages)
10              ai/json_parse.c       util/json          test_json_parse.c (repair, streaming)
11              ai/validation.c       util/json          test_validation.c (schema validate)
12              ai/stream.c           ai/types           test_stream.c (event construction)
13              ai/registry.c         ai/types, stream   test_registry.c (register/lookup)
14              ai/models.c           ai/types           test_models.c (static data, cost calc)
15              ai/transform.c        ai/types           test_transform.c (normalization)
16              ai/providers/anthropic.c  all of ai/*     test_anthropic.c (mock HTTP)
```

### Phase 2: Agent Loop

```
Build order     File                  Depends on         Tests with
──────────────────────────────────────────────────────────────────
17              agent/agent.c         ai/*               test_agent.c (state management)
18              agent/loop.c          agent/agent        test_loop.c (mock tools, mock stream)
19              agent/proxy.c         agent/agent        test_proxy.c (reconstruct partial)
```

### Phase 3: Minimal Harness (Print Mode)

```
Build order     File                  Depends on
──────────────────────────────────────────────────────
20              harness/config.c      util/fs
21              harness/auth.c        util/json, util/fs
22              harness/settings.c    util/json, config
23              harness/model_registry.c  ai/models, auth
24              harness/system_prompt.c   util/str, settings
25              harness/tools/bash.c  util/process, util/str
26              harness/tools/read.c  util/fs, util/str
27              harness/tools/write.c util/fs
28              harness/tools/edit.c  util/fs, util/str
29              harness/tools/grep.c  util/process
30              harness/tools/find.c  util/process
31              harness/tools/ls.c    util/fs
32              harness/modes/print.c harness/*, agent/*
33              main.c                harness/modes/print
```

**MILESTONE: rig -p "hello" works end-to-end.**

### Phase 4: Session & Resources

```
34              harness/session.c     util/json, ai/types
35              harness/compaction.c  session, agent
36              harness/skills.c      util/fs, util/str
37              harness/prompts.c     util/str
38              harness/themes.c      util/json, util/fs
39              harness/packages.c    util/process (shells out to npm/git)
40              harness/slash_commands.c  harness/*
```

### Phase 5: Extensions

```
41              harness/extensions/bus.c        util/json
42              harness/extensions/policy.c     extensions/types
43              harness/extensions/loader.c     all of extensions/*
44              harness/extensions/runner.c     loader, bus
45              harness/extensions/lua_bindings.c  runner + deps/lua
```

### Phase 6: Workflow Engine

```
46              harness/workflow/types.h         (header only)
47              harness/workflow/variables.c     util/json
48              harness/workflow/parser.c        types + deps/libyaml
49              harness/workflow/gates.c         types
50              harness/workflow/spawn.c         agent/*, types
51              harness/workflow/executor.c      all workflow/*
52              harness/workflow/checkpoint.c    executor
```

### Phase 7: TUI

```
53              tui/ansi.c            (standalone)
54              tui/unicode.c         deps/utf8proc
55              tui/terminal.c        (standalone, POSIX)
56              tui/keys.c            (standalone)
57              tui/tui.c             terminal, keys, ansi, unicode
58              tui/overlay.c         tui
59              tui/widgets/text.c    tui, ansi, unicode
60              tui/widgets/box.c     tui
61              tui/widgets/loader.c  tui
62              tui/widgets/input.c   tui, keys
63              tui/widgets/editor.c  tui, keys, input
64              tui/widgets/select.c  tui, keys
65              tui/widgets/markdown.c  tui, ansi, unicode
66              tui/widgets/image.c   tui, terminal
67              harness/modes/interactive.c  tui/*, harness/*
```

### Phase 8: Remaining Providers & RPC

```
68              ai/providers/openai_completions.c
69              ai/providers/openai_responses.c
70              ai/providers/google.c
71              ai/providers/google_vertex.c
72              ai/providers/google_gemini_cli.c
73              ai/providers/azure_openai_responses.c
74              ai/providers/openai_codex_responses.c
75              ai/providers/bedrock.c
76              ai/providers/mistral.c
77              harness/modes/rpc.c
```

### Phase 9: SDK & Polish

```
78              include/rig.h          (public API, wraps harness/*)
```

---

## 7. API Contract Summary

### 7.1 L0: Utilities

```c
// arena.h -- Pool allocator
Arena    arena_create(size_t initial_size);
void     arena_destroy(Arena *a);
void    *arena_alloc(Arena *a, size_t size);
void    *arena_alloc_zero(Arena *a, size_t size);
char    *arena_strdup(Arena *a, const char *s);
char    *arena_sprintf(Arena *a, const char *fmt, ...);
void     arena_reset(Arena *a);         // reuse without freeing backing

// str.h -- Dynamic string builder
Str      str_new(void);                 // empty string, heap
Str      str_from(const char *s);       // copy from C string
Str      str_from_arena(Arena *a);      // arena-backed string
void     str_free(Str *s);
void     str_append(Str *s, const char *data, size_t len);
void     str_append_cstr(Str *s, const char *cstr);
void     str_appendf(Str *s, const char *fmt, ...);
char    *str_cstr(const Str *s);        // NUL-terminated, borrowed
size_t   str_len(const Str *s);
void     str_clear(Str *s);

// vec.h -- Generic dynamic array (macro-based)
// Usage: Vec(int) nums; vec_init(&nums); vec_push(&nums, 42);
#define Vec(T)              struct { T *data; int count; int cap; }
#define vec_init(v)         ...
#define vec_push(v, item)   ...
#define vec_pop(v)          ...
#define vec_get(v, i)       ...
#define vec_free(v)         ...
#define vec_count(v)        ...

// hashmap.h -- String-keyed hash map
Hashmap *hashmap_create(int initial_cap);
void     hashmap_destroy(Hashmap *m);
void     hashmap_set(Hashmap *m, const char *key, void *value);
void    *hashmap_get(Hashmap *m, const char *key);
bool     hashmap_has(Hashmap *m, const char *key);
void     hashmap_remove(Hashmap *m, const char *key);
int      hashmap_count(Hashmap *m);
void     hashmap_iterate(Hashmap *m, void (*fn)(const char *key, void *value, void *ctx), void *ctx);

// json.h -- cJSON convenience wrappers
cJSON   *json_parse_file(const char *path);
int      json_write_file(const char *path, const cJSON *json);
cJSON   *json_get(const cJSON *root, const char *path);  // "foo.bar[0].baz"
char    *json_get_string(const cJSON *root, const char *path);  // borrowed
int      json_get_int(const cJSON *root, const char *path, int default_val);
double   json_get_double(const cJSON *root, const char *path, double default_val);
bool     json_get_bool(const cJSON *root, const char *path, bool default_val);

// http.h -- libcurl wrapper
typedef void (*HttpDataCallback)(const char *chunk, size_t len, void *ctx);
typedef struct {
    int status_code;
    char *body;          // only for non-streaming
    size_t body_len;
    Hashmap *headers;    // response headers
} HttpResponse;

int  http_get(const char *url, Hashmap *headers, HttpResponse *resp);
int  http_post(const char *url, Hashmap *headers, const char *body, size_t body_len, HttpResponse *resp);
int  http_post_stream(const char *url, Hashmap *headers, const char *body, size_t body_len,
                      HttpDataCallback on_data, void *ctx, HttpResponse *resp);
void http_response_free(HttpResponse *resp);

// SSE parser
typedef void (*SSEEventCallback)(const char *event_type, const char *data, void *ctx);
typedef struct SSEParser SSEParser;
SSEParser *sse_parser_create(SSEEventCallback on_event, void *ctx);
void       sse_parser_feed(SSEParser *p, const char *chunk, size_t len);
void       sse_parser_destroy(SSEParser *p);

// fs.h -- File operations
char    *fs_read_file(const char *path, size_t *len);     // malloc'd, caller frees
int      fs_write_file(const char *path, const char *data, size_t len);
int      fs_write_file_atomic(const char *path, const char *data, size_t len);  // temp+rename
bool     fs_exists(const char *path);
bool     fs_is_dir(const char *path);
int      fs_mkdir_p(const char *path);                     // recursive mkdir
char    *fs_resolve(const char *base, const char *relative);
char   **fs_list_dir(const char *path, int *count);       // malloc'd array, caller frees

// process.h -- Child process management
typedef struct {
    int pid;
    int exit_code;
    char *stdout_buf;
    size_t stdout_len;
    char *stderr_buf;
    size_t stderr_len;
    bool killed;
} ProcessResult;

typedef void (*ProcessOutputCallback)(const char *data, size_t len, bool is_stderr, void *ctx);

int  process_run(const char *command, const char **args, const char *cwd,
                 int timeout_ms, ProcessResult *result);
int  process_run_streaming(const char *command, const char **args, const char *cwd,
                           ProcessOutputCallback on_output, void *ctx,
                           int timeout_ms, ProcessResult *result);
void process_kill(int pid);
void process_result_free(ProcessResult *r);
```

### 7.2 L1: AI

```c
// types.h -- see DESIGN.md Section 3 (with corrections from discrepancy report)
// Key corrections:
//   Usage.cost must be struct { double input, output, cache_read, cache_write, total; }
//   Model.input must be char **input_types + int input_type_count (not bool supports_images)
//   StreamOptions must include all 10+ fields from actual TS

// stream.h
typedef void (*StreamCallback)(StreamEvent *event, void *userdata);

// registry.h
void             ai_register_provider(const char *api, ProviderStreamFn stream_fn,
                                      ProviderStreamSimpleFn stream_simple_fn);
ProviderStreamFn ai_get_provider_stream(const char *api);
ProviderStreamSimpleFn ai_get_provider_stream_simple(const char *api);
void             ai_unregister_provider(const char *api);

// models.h
Model   *models_get(const char *provider, const char *model_id);
Model  **models_get_all(const char *provider, int *count);
char   **models_get_providers(int *count);
void     models_calculate_cost(const Model *model, Usage *usage);  // fills cost struct
bool     models_supports_xhigh(const Model *model);
bool     models_are_equal(const Model *a, const Model *b);

// validation.h
int      validate_tool_arguments(const cJSON *schema, const cJSON *args,
                                 char **error_message);  // 0=valid

// json_parse.h
cJSON   *json_parse_repair(const char *json);
cJSON   *json_parse_streaming(const char *partial_json);

// transform.h
int      transform_messages(const Message *msgs, int count, const Model *target_model,
                            const Model *source_model, Message **out, int *out_count);
```

### 7.3 L2: Agent

```c
// agent.h
AgentState *agent_state_create(void);
void        agent_state_destroy(AgentState *state);

typedef void (*AgentEventCallback)(AgentEvent *event, void *userdata);

int  agent_prompt(AgentState *state, AgentMessage *messages, int count,
                  AgentLoopConfig *config, AgentEventCallback cb, void *userdata);
int  agent_continue(AgentState *state, AgentLoopConfig *config,
                    AgentEventCallback cb, void *userdata);
void agent_abort(AgentState *state);
void agent_reset(AgentState *state);

void agent_steer(AgentState *state, AgentMessage *message);
void agent_follow_up(AgentState *state, AgentMessage *message);
void agent_clear_queues(AgentState *state);
bool agent_has_queued_messages(AgentState *state);
```

### 7.4 L4: Harness

```c
// session.h
Session    *session_create(const char *cwd, const char *session_dir);
Session    *session_load(const char *path);
void        session_destroy(Session *s);
int         session_append(Session *s, SessionEntry *entry);
int         session_build_context(Session *s, AgentMessage **out, int *count,
                                  char **thinking_level, char **model_provider, char **model_id);
int         session_branch(Session *s, const char *entry_id, const char *position);
int         session_fork(Session *s, Session **new_session);
const char *session_get_leaf_id(Session *s);
SessionEntry *session_get_entry(Session *s, const char *id);
int         session_get_tree(Session *s, SessionTreeNode **root);
int         session_migrate(Session *s);  // in-place migration to current version
SessionInfo **session_list(const char *session_dir, int *count);

// settings.h
SettingsManager *settings_create(const char *global_path, const char *project_path);
void     settings_destroy(SettingsManager *sm);
cJSON   *settings_get(SettingsManager *sm, const char *key);
int      settings_set(SettingsManager *sm, const char *scope, const char *key, cJSON *value);
int      settings_flush(SettingsManager *sm, const char *scope);
Settings settings_get_merged(SettingsManager *sm);  // returns copy of merged settings struct

// model_registry.h
ModelRegistry *model_registry_create(const char *auth_path, const char *models_json_path);
void           model_registry_destroy(ModelRegistry *r);
Model         *model_registry_resolve(ModelRegistry *r, const char *pattern);
int            model_registry_get_arig_key(ModelRegistry *r, const char *provider, char **key);
Model        **model_registry_get_models(ModelRegistry *r, const char *provider, int *count);

// system_prompt.h
char *build_system_prompt(BuildSystemPromptOptions *options);  // malloc'd

// auth.h
int  auth_get_key(const char *provider, char **key);  // env, auth.json, or command
int  auth_store_key(const char *provider, const char *key);
int  auth_remove_key(const char *provider);

// compaction.h
bool should_compact(const AgentMessage *msgs, int count, const Model *model,
                    const CompactionSettings *settings);
int  compact(Session *s, AgentState *agent, const CompactionSettings *settings);
int  estimate_tokens(const AgentMessage *msgs, int count);
```

### 7.5 L3a: TUI

```c
// tui.h
TUI     *tui_create(Terminal *term);
void     tui_destroy(TUI *tui);
void     tui_render(TUI *tui);
void     tui_add_component(TUI *tui, Component *comp);
void     tui_remove_component(TUI *tui, Component *comp);
OverlayHandle tui_add_overlay(TUI *tui, Component *comp, OverlayOptions *opts);
void     tui_remove_overlay(TUI *tui, OverlayHandle handle);
void     tui_set_focus(TUI *tui, Component *comp);
void     tui_handle_input(TUI *tui, const char *data);

// terminal.h
void terminal_enter_raw_mode(void);
void terminal_exit_raw_mode(void);
void terminal_get_size(int *cols, int *rows);
void terminal_write(const char *data, size_t len);
void terminal_flush(void);

// keys.h
ParsedKey key_parse(const char *raw, size_t len);
bool      key_matches(const char *raw, size_t len, const char *key_id);
```

### 7.6 L3c: Extensions

```c
// loader.h
int  extensions_discover(const char **paths, int count, LoadedExtension **out, int *out_count);
int  extensions_load(LoadedExtension *exts, int count, RigExtensionAPI *api);
void extensions_unload(LoadedExtension *exts, int count);

// runner.h
ExtensionRunner *runner_create(void);
void  runner_destroy(ExtensionRunner *r);
int   runner_emit(ExtensionRunner *r, const char *event, cJSON *data, cJSON **result);
void  runner_register_hook(ExtensionRunner *r, const char *event, int priority,
                           HookHandler handler, void *ctx);

// bus.h
void  bus_publish(EventBus *bus, const char *channel, cJSON *data);
void  bus_subscribe(EventBus *bus, const char *channel,
                    void (*handler)(cJSON *data, void *ctx), void *ctx);
```

---

## 8. Configuration Cascade

### 8.1 Merge Order (lowest to highest precedence)

```
Defaults (compiled into settings.c)
    │
    v
Global settings (~/.rig/agent/settings.json)
    │
    v
Project settings (.rig/settings.json)
    │
    v
CLI arguments (--model, --thinking, --tools, etc.)
    │
    v
Extension overrides (rig.setModel(), rig.setThinkingLevel(), etc.)
    │
    v
Final effective settings
```

### 8.2 Merge Semantics

| Setting Type | Merge Rule |
|-------------|------------|
| Scalar (string, number, bool) | Higher precedence wins completely |
| Nested object (compaction, retry, terminal, images) | Shallow merge: higher precedence fields override, unset fields inherit |
| Array (packages, extensions, skills, prompts, themes, enabledModels) | Higher precedence replaces entire array |
| Undefined/missing | Inherited from lower precedence |

### 8.3 Settings File Watching

- `SettingsManager` watches global and project settings files for changes
- On change: re-read, re-merge, emit "settings_changed" (internal, not extension event)
- Consumers (model registry, theme, compaction) react to changes
- Settings file is locked with advisory lock during writes (proper-lockfile equivalent)

### 8.4 CLI to Settings Mapping

```
--model <pattern>           -> settings.defaultModel (+ immediate model resolve)
--provider <name>           -> settings.defaultProvider
--thinking <level>          -> settings.defaultThinkingLevel
--models <patterns>         -> settings.enabledModels
--tools <names>             -> tool filter (not persisted to settings)
--no-tools                  -> suppress all tools
--no-builtin-tools          -> suppress builtin tools only
--extensions <paths>        -> settings.extensions (session-scoped)
--skills <paths>            -> settings.skills (session-scoped)
--prompts <paths>           -> settings.prompts (session-scoped)
--themes <paths>            -> settings.themes (session-scoped)
--session <path>            -> session file path (not in settings)
--continue                  -> resume most recent session
--resume                    -> interactive session picker
--fork                      -> fork from specified entry
--mode json                 -> json output mode
--mode rpc                  -> RPC mode
-p / --print                -> print mode
--export <path>             -> export session to file
--share                     -> share as gist
--import <path>             -> import session
--session-dir <path>        -> settings.sessionDir
```

### 8.5 Path Resolution

```
Config directory:    ~/.rig/agent/
Sessions directory:  ~/.rig/agent/sessions/--<encoded-cwd>--/
Auth file:           ~/.rig/agent/auth.json
Global settings:     ~/.rig/agent/settings.json
Models override:     ~/.rig/agent/models.json
Keybindings:         ~/.rig/agent/keybindings.json
Project settings:    <cwd>/.rig/settings.json
Project extensions:  <cwd>/.rig/extensions/
Project skills:      <cwd>/.rig/skills/
Project prompts:     <cwd>/.rig/prompts/
Project themes:      <cwd>/.rig/themes/
Context files:       Walk from <cwd> to root: .rig/CONTEXT.md, AGENTS.md
Package install dir: ~/.rig/agent/packages/<package-name>/
Workflow checkpoints: ~/.rig/agent/workflows/checkpoints/
```

---

## 9. Extension ABI Contract

### 9.1 ABI Version

```c
#define RIG_ABI_VERSION 1

// Extensions must export this. Pi refuses to load mismatched versions.
RIG_EXPORT int rig_abi_version(void) { return RIG_ABI_VERSION; }
```

ABI version increments when:
- Any struct in `rig.h` changes size or field order
- Any function pointer signature in `RigExtensionAPI` changes
- Any enum in `rig.h` adds/removes/reorders values

ABI version does NOT increment when:
- New functions are APPENDED to `RigExtensionAPI` (struct grows, old offsets stable)
- New enum values are APPENDED at end
- Bug fixes in Pi internals

### 9.2 Guaranteed Stable Structs

These structs are part of the ABI and must not change layout without a version bump:

```c
// All fields, order, and sizes are ABI-stable:
typedef struct RigExtensionAPI { ... };  // function pointer table
typedef struct ContentBlock { ... };
typedef struct Message { ... };
typedef struct AgentMessage { ... };
typedef struct Tool { ... };
typedef struct Model { ... };
typedef struct StreamEvent { ... };
typedef struct AgentEvent { ... };
```

### 9.3 Opaque/Internal Structs

These are passed as `void *` or opaque pointers. Extensions must not inspect their internals:

```c
// Extensions receive but must not dereference:
typedef struct AgentState AgentState;        // opaque
typedef struct Session Session;              // opaque
typedef struct SettingsManager SettingsManager; // opaque
typedef struct ExtensionRunner ExtensionRunner; // opaque
typedef struct WorkflowContext WorkflowContext; // opaque
```

### 9.4 Extension Entry Points

```c
// Required: initialization
RIG_EXPORT void rig_extension_init(RigExtensionAPI *rig);

// Required: ABI version check
RIG_EXPORT int rig_abi_version(void);

// Optional: dependency declaration
RIG_EXPORT const char **rig_extension_depends(int *count);

// Optional: metadata
RIG_EXPORT const char *rig_extension_name(void);
RIG_EXPORT const char *rig_extension_version(void);
RIG_EXPORT const char *rig_extension_description(void);
```

### 9.5 Calling Convention

- All function pointers in `RigExtensionAPI` use C calling convention (`cdecl`)
- All strings are UTF-8, NUL-terminated
- All JSON data is passed as `cJSON *` (from vendored cJSON, same version as rig)
- Extensions must NOT free `cJSON *` values received from Pi (borrowed)
- Extensions must allocate `cJSON *` values returned to Pi (Pi takes ownership)
- All callbacks receive a `void *ctx` userdata pointer

### 9.6 Lua Extension ABI

Lua extensions have no binary ABI -- they use the Lua C API:

```lua
-- Required: init function
function init(rig)
    -- rig is a Lua table with methods matching RigExtensionAPI
    rig:on("event_name", handler_function)
    rig:register_tool(tool_table)
    rig:register_command("name", handler_function)
end

-- Optional: dependency declaration
function depends()
    return { "other-extension" }
end
```

Lua sandbox restrictions:
- Separate `lua_State` per extension (no cross-extension state)
- Blocked: `os`, `io`, `loadfile`, `dofile`, `debug.*`, `package.loadlib`
- Allowed: `string`, `table`, `math`, `utf8`, Pi-provided `json` module
- Memory limit: 50MB per extension (custom allocator)
- CPU limit: 10,000 instructions per hook call (`lua_sethook`)
- Extensions opt out of sandbox via manifest `sandbox: false`

### 9.7 YAML Workflow ABI

YAML workflows have no code ABI. They are validated against a schema:

```yaml
# Required fields
name: string          # unique workflow name
steps: array          # at least one step

# Optional fields
description: string
trigger: string       # slash command trigger
defaults: object      # default variable values
depends: array        # extension dependencies

# Per-step required fields
steps[].name: string  # unique within workflow
steps[].type: enum    # prompt|tool|bash|gate|condition|parallel|join|
                      # sub_workflow|transform|loop|retry|emit|
                      # wait_event|spawn_session|http|checkpoint
```

---

## 10. Testing Strategy

### 10.1 Unit Test Boundaries

| Module | Testable in Isolation | Mock Boundary | Test Data |
|--------|----------------------|---------------|-----------|
| `util/arena.c` | Yes | None | Allocation patterns, edge cases |
| `util/str.c` | Yes | None | Append, format, UTF-8 strings |
| `util/hashmap.c` | Yes | None | Insert, lookup, remove, collisions |
| `util/json.c` | Yes | cJSON (vendored, no mock) | Path access, type coercion |
| `ai/types.c` | Yes | None | Construct/free messages, content blocks |
| `ai/json_parse.c` | Yes | None | Malformed JSON, partial JSON, repair |
| `ai/validation.c` | Yes | None | Schema validation, type coercion |
| `ai/transform.c` | Yes | None | Message normalization, tool ID truncation |
| `ai/models.c` | Yes | None | Model lookup, cost calculation |
| `agent/loop.c` | Yes | Mock stream fn + mock tools | Full loop with scripted responses |
| `harness/session.c` | Yes | Mock filesystem (in-memory JSONL) | Tree traversal, branch, fork, compact |
| `harness/settings.c` | Yes | Mock filesystem | Merge semantics, deep merge, defaults |
| `harness/skills.c` | Yes | Mock filesystem | Discovery, frontmatter parse, XML format |
| `harness/prompts.c` | Yes | None | Argument substitution: $1, $@, ${@:N:L} |
| `harness/themes.c` | Yes | Mock filesystem | Color parsing, variable resolution |
| `tui/ansi.c` | Yes | None | SGR tracking, reset/restore sequences |
| `tui/unicode.c` | Yes | None | Display width, grapheme boundaries |
| `tui/keys.c` | Yes | None | Key parsing: Kitty, legacy, mouse, paste |
| `tui/tui.c` | Partial | Mock terminal | Diff rendering, line comparison |
| `workflow/variables.c` | Yes | None | Variable resolution, expression eval |
| `workflow/parser.c` | Yes | None | YAML to workflow graph |
| `extensions/bus.c` | Yes | None | Pub/sub, channel matching |

### 10.2 Integration Test Boundaries

| Test | Components Involved | What It Proves |
|------|-------------------|----------------|
| Provider round-trip | `providers/*.c` + `http.c` + real API | SSE parsing, message construction, auth |
| Agent loop + tools | `loop.c` + `tools/*.c` | Tool execution, result handling, multi-turn |
| Session persistence | `session.c` + filesystem | JSONL write/read, tree integrity, migration |
| Print mode E2E | `main.c` + `print.c` + all | Full pipeline from CLI to stdout |
| Extension loading | `loader.c` + test .so | dlopen, ABI check, init, hook dispatch |
| Lua extension | `lua_bindings.c` + test .lua | Sandbox, API binding, event handling |
| Workflow execution | `executor.c` + mock tools | Sequential, parallel, gates, loops |
| TUI rendering | `tui.c` + `terminal.c` (mock) | Component render, diff, overlay |
| Settings cascade | `settings.c` + `main.c` | CLI overrides, project overrides, merge |
| Compaction | `compaction.c` + `session.c` + mock LLM | Token estimation, summary, context rebuild |

### 10.3 Mock Infrastructure

```c
// Mock stream function for agent loop testing
// Returns pre-scripted AssistantMessages
typedef struct {
    Message *responses;     // array of canned responses
    int count;
    int current;
} MockStreamState;

int mock_stream(const Model *model, const Message *msgs, int msg_count,
                const char *system_prompt, const Tool *tools, int tool_count,
                const StreamOptions *options, StreamCallback callback, void *userdata);

// Mock tool for agent loop testing
// Returns pre-scripted results
typedef struct {
    const char *name;
    ContentBlock *result_content;
    int result_count;
    bool terminate;
} MockTool;

// Mock filesystem for session/settings testing
typedef struct {
    Hashmap *files;         // path -> content mapping
} MockFS;
```

### 10.4 Test Data Sources

- **Provider SSE responses**: Capture real SSE streams from each provider, store as test fixtures in `test/fixtures/sse/`
- **Session files**: Sample JSONL files for each version (v1, v2, v3) in `test/fixtures/sessions/`
- **Tool schemas**: JSON Schema test cases in `test/fixtures/schemas/`
- **YAML workflows**: Sample workflow definitions in `test/fixtures/workflows/`
- **Unicode test strings**: CJK, emoji, combining marks, zero-width joiners in `test/fixtures/unicode.txt`
- **Malformed JSON**: Truncated, extra commas, missing quotes in `test/fixtures/json/`

### 10.5 Test Framework

Use a minimal custom test framework (or adapt Unity/greatest):

```c
// test/test.h
#define TEST(name) static void test_##name(void)
#define ASSERT_EQ(a, b) ...
#define ASSERT_STR_EQ(a, b) ...
#define ASSERT_TRUE(x) ...
#define ASSERT_NULL(x) ...
#define ASSERT_NOT_NULL(x) ...
#define RUN_TEST(name) ...
```

### 10.6 Coverage Targets

| Layer | Target | Rationale |
|-------|--------|-----------|
| L0: Utilities | 90%+ | Foundation, must be rock-solid |
| L1: AI types/validation | 85%+ | Correctness-critical |
| L1: Providers | 70%+ | Hard to mock fully, rely on integration tests |
| L2: Agent loop | 85%+ | Core algorithm, must handle all edge cases |
| L4: Session | 85%+ | Data integrity critical |
| L4: Settings | 80%+ | Merge semantics must be correct |
| L4: Tools | 75%+ | External process interaction limits coverage |
| L3a: TUI | 70%+ | Visual correctness hard to assert programmatically |
| L3b: Workflow | 80%+ | Complex state machine, needs thorough testing |
| L3c: Extensions | 75%+ | dlopen/Lua interaction limits unit testing |

---

## Appendix A: Discrepancies with Original TS (Corrections to DESIGN.md)

This architecture accounts for the following corrections identified by cross-referencing
DESIGN.md against the actual pi-mono source code. Implementers should treat this list
as errata for DESIGN.md.

1. **4 missing providers**: azure-openai-responses, openai-codex-responses, google-gemini-cli, google-vertex
2. **Usage.cost must be a struct** with 5 fields, not a single double
3. **Dual stream API**: each provider registers both `stream` and `streamSimple`
4. **Role-specific content types**: UserMessage, AssistantMessage, ToolResultMessage have different valid content types
5. **4 custom AgentMessage types**: bashExecution, custom, branchSummary, compactionSummary
6. **StreamOptions missing 10+ fields**: transport, cacheRetention, sessionId, onPayload, onResponse, headers, timeoutMs, maxRetries, maxRetryDelayMs, metadata
7. **ThinkingBudgets**: per-level token budget overrides, not just integer level
8. **Extension API missing ~20 methods**: see DESIGN.md errata
9. **28 extension event types** (not 12)
10. **Context overflow detection**: 16+ regex patterns for provider-specific errors
11. **Provider registration**: full OAuth support, custom stream handlers, unregister
12. **Slash commands**: 10 differ from DESIGN.md list
13. **Model.input**: array of capability strings, not boolean flag
14. **Output guard**: stdout takeover, not cost limiting
15. **Settings**: 15+ fields missing from DESIGN.md
16. **TUI**: 6+ missing components and subsystems

See the full discrepancy report for details.
