# Pi-C Build Continuation State

## What This Project Is
Rewriting pi-mono (TypeScript coding agent) in C. Full design in DESIGN.md (2706 lines), architecture in ARCHITECTURE.md (1426 lines).

## What's Built So Far

### Phase 0: Utilities (COMPLETE — 116 tests pass)
- `src/util/arena.h/.c` — arena allocator
- `src/util/str.h/.c` — dynamic string builder
- `src/util/vec.h` — generic dynamic array (header-only)
- `src/util/hashmap.h/.c` — Robin Hood hashmap
- `src/util/json.h/.c` — cJSON path-access wrapper
- `src/util/http.h/.c` — libcurl + SSE stream parser
- `src/util/fs.h/.c` — file system ops
- `src/util/process.h/.c` — child process management
- `src/util/log.h` — logging macros
- `test/test.h` — test framework
- `test/test_util.c` — 116 tests

### Phase 1: AI Core (COMPLETE — 53 tests pass)
- `src/ai/types.h/.c` — Message, ContentBlock, Model, StreamEvent, Tool types
- `src/ai/registry.h/.c` — provider registration
- `src/ai/validation.h/.c` — JSON Schema validation + type coercion
- `src/ai/json_parse.h/.c` — streaming/repair JSON parser
- `src/ai/transform.h/.c` — cross-provider message normalization
- `src/ai/models.h/.c` — 5 hardcoded models (Opus/Sonnet/Haiku/GPT-4o)
- `src/ai/providers/anthropic.h/.c` — full Anthropic SSE streaming provider
- `test/test_ai.c` — 53 tests

### Phase 2: Agent Loop (COMPLETE — 18 tests pass)
- `src/agent/agent.h/.c` — AgentState, MessageQueue, agent_prompt/continue/abort, tool execution (sequential + parallel via pthreads), before/after tool call hooks, stream bridge
- `test/test_agent.c` — 18 tests with mock provider + mock tools

### Phase 3: Minimal Harness + Print Mode (COMPLETE — 29 tests pass)
- `src/harness/config.h/.c` — path resolution (~/.pi/agent/, .pi/)
- `src/harness/auth.h/.c` — API key from env vars (8 providers)
- `src/harness/system_prompt.h/.c` — system prompt builder
- `src/harness/tools/tools.h` — tool creation functions
- `src/harness/tools/bash_tool.c` — bash execution tool
- `src/harness/tools/read_tool.c` — file read tool
- `src/harness/tools/write_tool.c` — file write tool
- `src/harness/tools/edit_tool.c` — search/replace edit tool
- `src/harness/tools/grep_tool.c` — grep wrapper
- `src/harness/tools/ls_tool.c` — directory listing
- `src/harness/modes/print.h/.c` — print mode (single-shot)
- `src/main.c` — CLI entry point with arg parsing
- `test/test_harness.c` — 29 tests
- **Binary: `./pi` works! `./pi -p "prompt"` runs end-to-end**

### Phase 4: Sessions & Resources (IN PROGRESS — partially broken)
- `src/harness/session.h/.c` — JSONL session tree (written by agent, 579 lines)
- `src/harness/skills.h/.c` — Agent Skills standard loader
- `src/harness/prompts.h/.c` — prompt template expansion ($1, $@, ${@:N:L})
- `src/harness/themes.h/.c` — JSON theme loading + ANSI color resolution
- `src/harness/packages.h/.c` — npm/git/local package install/remove/list
- `test/test_session.c` — 16 tests, SEGFAULTING (see bug below)
- `test/test_skills.c` — passes (agent format, not test.h format)
- `test/test_prompts.c` — 14 tests pass (fixed literal_dollar test)
- `test/test_themes.c` — passes (agent format)

## Current Bug to Fix

`test/test_session.c` segfaults because `session_create(NULL)` returns NULL (the agent's implementation rejects NULL sessions_dir). Tests that used `session_create(NULL)` for in-memory sessions were already partially fixed to use `TEST_DIR="/tmp/pi_test_sessions"` but need cleanup_test_dir() calls in the right places. After fixing, rebuild with `rm -f test/test_session && make test/test_session` and run.

## Remaining Phases (from DESIGN.md)

### Phase 5: Extension System (Week 10-12)
- Shared library loading (dlopen, ABI version check)
- PiExtensionAPI core surface (hooks, tools, commands, providers)
- Hook chains with priority and short-circuit
- Event bus (publish/subscribe/request-reply)
- Lua 5.4 embedding + sandboxed Pi API bindings
- YAML workflow parser (libyaml)

### Phase 6: Workflow Engine (Week 13-15)
- Workflow graph builder (YAML → step graph)
- Workflow executor (sequential + parallel steps)
- Variable system & expression evaluator
- Gate system, sub-agent spawning, loops, checkpointing

### Phase 7: TUI (Week 16-18)
- Terminal abstraction, keyboard input (Kitty protocol)
- Differential rendering engine
- Widgets: Text, Input, Box, Loader, Markdown, Editor, SelectList
- Overlay system, image support (Kitty/iTerm2)

### Phase 8: RPC & Additional Providers (Week 19-20)
- RPC mode (JSONL stdio)
- OpenAI Completions/Responses providers
- Google, Bedrock, Mistral providers

### Phase 9: Polish (Week 21+)
- SDK mode, cross-platform, performance, docs

## Workflow Per Phase

For EACH phase:
1. Read DESIGN.md for that phase's spec
2. Implement all modules
3. Write comprehensive tests: regular cases, edge cases, adversarial inputs
4. Fix all failures
5. Run FULL regression across ALL test suites: `for t in test/test_*; do ./$t 2>&1 | tail -1; done`
6. Only proceed to next phase when ALL tests pass with 0 failures

## Build Commands
```
make clean && make all    # build library + pi binary
make test/test_NAME       # build specific test
./test/test_NAME          # run specific test
make test                 # build and run all tests
```

## Deps
- vendored: deps/cjson/ (cJSON)
- system: libcurl, pthreads
- NOT yet vendored: lua, libyaml (needed for Phase 5-6)
