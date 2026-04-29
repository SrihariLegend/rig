<p align="center">
  <img src="assets/screenshot.png" alt="Rig" width="700">
</p>

<div align="center">

**~23K lines of C. 566KB binary. Zero runtime deps. Every major LLM provider. Lua extensions.**

</div>

```
$ curl -fsSL https://raw.githubusercontent.com/SrihariLegend/rig/main/install.sh | sh
$ rig auth
$ rig
```

---

```
 rig v0.1.0          session: 7f3a2c1          claude-opus-4-6 (bedrock)

 you  Why should I use rig instead of [other tool]?

 rig  Here's the short version:

      ┌─────────────────────────────────────────────────────────────────┐
      │                                                                 │
      │  Other tools           Rig                                      │
      │  ─────────────         ───────────────────────────              │
      │  Electron/Node/Python  Single C binary, 566KB                   │
      │  1-2 GB installed      Zero runtime dependencies                │
      │  One provider          Anthropic, OpenAI, Google, Bedrock,      │
      │                        Mistral, DeepSeek, xAI, Groq, OpenRouter │
      │  Plugin SDK + build    Drop a .lua file, done                   │
      │  Startup: seconds      Startup: <1ms                           │
      │                                                                 │
      └─────────────────────────────────────────────────────────────────┘

      Copy it to a machine. Run it. That's the whole install.
```

---

## Tools

```
 you  /tools

 rig  7 tools available:

      bash        Execute shell commands with timeout and output capture
      read        Read files with line numbers, offsets, and limits
      write       Create or overwrite files
      edit        Surgical find-and-replace edits
      grep        Regex search across files
      ls          List directory contents
      introspect  Query rig's own state (tools, config, extensions)

      Every tool has a permission system. Rig asks before writing
      to disk or running commands. Trust rules are configurable
      per tool, per path.
```

## Providers

```
 you  /model list

 rig  Available models:

      Provider      Models                                         Auth
      ───────────   ──────────────────────────────────────────     ──────────────
      Anthropic     Claude Opus 4.7, Opus 4.6, Sonnet 4.6,        API key
                    Haiku 4.5
      OpenAI        GPT-4o                                         API key
      Google        Via model registry                              API key
      AWS Bedrock   Claude Opus 4.6, Sonnet 4.5, Haiku 4.5        AWS credentials
      Mistral       Via model registry                              API key

      Also: DeepSeek, xAI, Groq, OpenRouter. Run `rig auth` to configure.

 you  /model sonnet

 rig  Switched to claude-sonnet-4-6 (anthropic)
```

## Terminal UI

```
 you  What's the TUI like?

 rig  ┌─ Lantern Rendering ──────────────────────────────────────────┐
      │                                                               │
      │  Warm-to-cool color gradient that fades with distance from    │
      │  the cursor. Each character has its own brightness. It gives  │
      │  the terminal a sense of depth and focus.                     │
      │                                                               │
      ├─ Also ────────────────────────────────────────────────────────┤
      │                                                               │
      │  Markdown      code blocks, bold, italic, lists, headings    │
      │  Scrollback    mouse wheel, Page Up/Down, vim keys           │
      │  Themes        JSON color schemes, hot reload via /theme     │
      │  Spinner       visual feedback during tool execution         │
      │  Responsive    handles resize, adapts to terminal width      │
      │                                                               │
      └───────────────────────────────────────────────────────────────┘
```

## Sessions

```
 you  /sessions

 rig  Recent sessions:

      ID          Name                          Last active
      ──────────  ────────────────────────────  ─────────────
      7f3a2c1     fix auth middleware            2 hours ago
      a91bc4e     refactor database layer        yesterday
      3de88f0     add webhook support            3 days ago

      Enter ID to resume, or press ESC to cancel.

 you  7f3a2c1

 rig  Resumed session: fix auth middleware (12 messages, 3 tool calls)
```

## Modes

```
 Interactive    rig                         Full TUI conversation
 Print          rig -p "prompt"             One off, stdout
 JSON           rig --json -p "prompt"      Structured event output
 RPC            (internal)                  Editor/tool integration

 $ echo "explain this" | rig -p -              works with pipes
 $ rig -p "list todos" >> tasks.md             works with redirects
 $ rig --json -p "analyze" | jq '.content'     works with jq
```

---

## Lua Extensions

Rig exposes **8 primitives** to Lua, mathematically proven to be the minimal complete set for unbounded extensibility:

```
 Primitive               What it does
 ──────────────────────  ───────────────────────────────
 rig.exec(cmd)           Run a shell command
 rig.completion(params)  Call any LLM
 rig.print(text)         Output to the TUI
 rig.input(prompt)       Read user input
 rig.hook(event, fn)     React to events
 rig.unhook(handle)      Remove a hook
 rig.get(ns, key)        Read state
 rig.set(ns, key, val)   Write state
```

No framework. No build step. Drop a `.lua` file in `.rig/extensions/` and it loads on startup.

```lua
-- custom slash command
rig.set("commands", "deploy", function(args)
    local r = rig.exec("git push origin main")
    rig.print(r.ok and "deployed" or "failed: " .. r.stdout)
end)

-- custom tool the LLM can call
rig.set("tools", "weather", {
    description = "Get weather for a city",
    params = { city = { type = "string", description = "City name" } },
    run = function(p)
        local r = rig.exec("curl -s 'wttr.in/" .. p.city .. "?format=3'")
        return r.stdout
    end
})

-- inject context into the system prompt
local r = rig.exec("cat README.md | head -20")
if r.ok then
    rig.set("prompts", "context", "Project README:\n" .. r.stdout)
end
```

Full documentation: [`docs/extensions.md`](docs/extensions.md)

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│                     rig                          │
│                  (566KB binary)                  │
├──────────────┬────────────┬─────────────────────┤
│   rig-ai     │  rig-agent │    rig-harness      │
│  providers   │    loop    │   tools, sessions   │
│  streaming   │    tool    │   permissions       │
│  transform   │   dispatch │   extensions        │
├──────────────┴────────────┴─────────────────────┤
│                   rig-tui                        │
│      lantern · markdown · scrollback            │
├─────────────────────────────────────────────────┤
│               Lua extensions                     │
│          8 primitives · sandboxed               │
└─────────────────────────────────────────────────┘

src/
├── ai/            LLM providers (Anthropic, OpenAI, Google, Bedrock, Mistral)
├── agent/         Agent loop: stream, tool calls, execute, repeat
├── harness/       CLI harness: auth, sessions, tools, permissions, extensions
│   ├── tools/     Built-in tools (bash, read, write, edit, grep, ls, introspect)
│   ├── modes/     Interactive, print, RPC
│   └── extensions/  Hook system, event bus, Lua bridge
├── tui/           Terminal UI: lantern renderer, markdown, keyboard, scrollback
└── util/          Arena allocator, strings, hashmap, HTTP, JSON, process
```

23K lines of C. No generated code.

---

## Install

```bash
# one line install
curl -fsSL https://raw.githubusercontent.com/SrihariLegend/rig/main/install.sh | sh

# or build from source (needs: C11 compiler, libcurl, libssl, zlib)
git clone https://github.com/SrihariLegend/rig.git
cd rig
make
sudo make install
```

```
 Build deps      C compiler, libcurl-dev, libssl-dev, zlib-dev
 Runtime deps    libcurl, libssl, zlib (present on virtually every Linux system)
 Vendored        Lua 5.4, cJSON, libyaml, md4c (zero install)
```

---

## Docs

```
 docs/extensions.md      Lua extensions: 8 primitives, namespaces, sandbox
 docs/configuration.md   Settings, permissions, trust rules, directory layout
 docs/sessions.md        Session persistence, branching, context reconstruction
 docs/workflows.md       YAML/JSON workflow engine: 16 step types, expressions
 docs/themes.md          Theme format: variables, 51 color tokens
 docs/prompts.md         Prompt templates: frontmatter, variable substitution
```

---

## Contributing

```bash
make          # build
make test     # run tests
make clean    # clean
```

The codebase is intentionally small and readable. Every subsystem fits in your head.

## License

MIT
