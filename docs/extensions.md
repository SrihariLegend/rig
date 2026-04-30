# Writing Rig Extensions

Extensions are Lua 5.4 scripts in `.rig/extensions/`. They're loaded automatically on startup.

## Quick Start

Create `.rig/extensions/myext.lua`:

```lua
rig.set("commands", "greet", function(args)
    rig.print("hello!")
end)
```

Restart rig, type `/greet`.

## Primitives

| Primitive | What it does |
|-----------|-------------|
| `rig.exec(cmd, opts?)` | Run a shell command, get `{ok, stdout, exit_code, timed_out}` |
| `rig.completion(params)` | Call the LLM - see [Completion](#completion) |
| `rig.print(text, opts?)` | Show text in the TUI. `{error=true}` for error styling |
| `rig.input(prompt)` | Block and read a line of user input |
| `rig.hook(event, fn, priority?)` | React to events - returns a handle string |
| `rig.unhook(handle)` | Remove a hook by handle |
| `rig.get(ns, key?)` | Read state - see [Namespaces](#namespaces) |
| `rig.set(ns, key, value)` | Write state - see [Namespaces](#namespaces) |
| `rig.read_file(path)` | Read file contents, returns string or nil+error |
| `rig.write_file(path, content)` | Write file, returns boolean |
| `rig.publish(topic, data)` | Publish event to the event bus |
| `rig.subscribe(topic, fn)` | Subscribe to event bus topic |

## Namespaces

### `rig.get(ns, key?)`

| Namespace | Key | Returns |
|-----------|-----|---------|
| `"config"` | `nil` | Table: `{model, model_id, provider, context_window, max_output_tokens, cwd}` |
| `"config"` | `"model"` | Model name string |
| `"config"` | `"cwd"` | Working directory string |
| `"messages"` | `nil` | Array of `{role, content}` - full conversation history |
| `"messages"` | `"count"` | Number of messages |
| `"messages"` | `"tokens"` | Estimated token count of all messages |
| `"messages"` | `1`, `2`, ... | Single message by index (1-indexed) |
| `"prompts"` | `nil` | Full assembled system prompt string |
| `"prompts"` | `"tokens"` | Estimated token count of system prompt |
| `"tools"` | `nil` | Array of tool name strings |
| `"tools"` | `"name"` | `{name, description}` for a specific tool |
| `"settings"` | key | JSON value from settings |
| `"state"` | key | JSON value from extension state store |

### `rig.set(ns, key, value)`

| Namespace | Key | Value | Effect |
|-----------|-----|-------|--------|
| `"commands"` | name | function | Register `/name` slash command |
| `"tools"` | name | table | Register a tool the LLM can call |
| `"tools"` | name | `nil` | Remove a tool |
| `"messages"` | `"append"` | `{role, content}` | Add a message to history |
| `"messages"` | `"clear"` | - | Clear conversation history |
| `"messages"` | `"splice"` | `{start, delete, messages}` | Replace/remove/insert messages |
| `"prompts"` | name | text | Inject a system prompt fragment |
| `"prompts"` | name | `nil` | Remove a system prompt fragment |
| `"settings"` | key | value | Store a setting |
| `"state"` | key | value | Store extension state |

## Hook Events

The agent loop fires hooks at key points. Extensions can observe, modify, or control behavior by returning tables from hook handlers.

### Available Events

| Event | When | Data |
|-------|------|------|
| `init` | Startup (once) | none |
| `turn_start` | Beginning of each loop turn | `{message_count}` |
| `pre_api_call` | Before LLM call | `{message_count, model, max_tokens, temperature}` |
| `post_api_call` | After LLM response | `{content_count, tool_call_count, stop_reason, input_tokens, output_tokens}` |
| `pre_tool` | Before each tool executes | `{tool, id, args}` |
| `post_tool` | After each tool completes | `{tool, id, is_error}` |
| `turn_end` | After tool results added | `{message_count, has_tool_calls}` |

### Hook Return Values

Return `true` to continue, `false` to stop the hook chain, or a **table** to control behavior:

**From `pre_api_call`:**
```lua
rig.hook("pre_api_call", function(event, data)
    return {max_tokens = 8192}       -- override output limit
    -- return {temperature = 0}      -- override temperature
    -- return {skip = true}          -- skip this LLM call entirely
    -- return {abort = true}         -- stop the agent loop
end)
```

**From `pre_tool`:**
```lua
rig.hook("pre_tool", function(event, data)
    if data.tool == "bash" and dangerous(data.args) then
        return {block = true, reason = "blocked by safety extension"}
    end
    -- return {modify_args = {command = "echo safe"}}  -- override args
    return true
end)
```

**From `post_tool`:**
```lua
rig.hook("post_tool", function(event, data)
    -- return {override_result = "truncated output"}  -- replace tool output
    return true
end)
```

**From `turn_end`:**
```lua
rig.hook("turn_end", function(event, data)
    if not data.has_tool_calls and should_continue() then
        return {
            continue_loop = true,
            inject_messages = {
                {role = "user", content = "Continue."}
            }
        }
    end
    return true
end)
```

### Basic Example

```lua
local h = rig.hook("pre_tool", function(event, data)
    rig.print("[hook] tool: " .. data.tool)
    return true
end)

-- later:
rig.unhook(h)
```

## Registering Tools

```lua
rig.set("tools", "weather", {
    description = "Get current weather for a city",
    params = {
        city = {type = "string", description = "City name"},
    },
    run = function(p)
        local r = rig.exec("curl -s 'wttr.in/" .. p.city .. "?format=3'")
        return r.stdout
    end
})
```

## Completion

Call the LLM from an extension:

```lua
local response = rig.completion({
    system = "You are a translator.",
    messages = {
        {role = "user", content = "Translate to French: hello"},
    },
    max_tokens = 100,
})
rig.print("Translation: " .. response)
```

### Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `messages` | array | required | `{role, content}` pairs |
| `system` | string | nil | System prompt |
| `model` | string | current model | Substring match (e.g. `"haiku"`, `"sonnet"`) |
| `max_tokens` | integer | model default | Max output tokens |
| `temperature` | number | -1 (provider default) | 0.0-1.0, ignored if provider doesn't support |

```lua
-- Use a cheap model for background tasks
local summary = rig.completion({
    model = "haiku",
    temperature = 0,
    max_tokens = 200,
    system = "Summarize in one sentence.",
    messages = {{role = "user", content = long_text}},
})
```

## Message Splicing

Replace, remove, or insert messages in the conversation history:

```lua
-- Remove messages 3 through 7 (delete 5 starting at index 3)
rig.set("messages", "splice", {start = 3, delete = 5})

-- Replace messages 2-4 with a single summary
rig.set("messages", "splice", {
    start = 2,
    delete = 3,
    messages = {
        {role = "user", content = "[earlier conversation summarized]"},
    }
})

-- Insert at position 1 without deleting
rig.set("messages", "splice", {start = 1, delete = 0, messages = {
    {role = "user", content = "Context: ..."}
}})
```

Cannot splice while the model is streaming (returns error).

## Event Bus

Extensions can communicate via publish/subscribe:

```lua
-- Publisher extension
rig.publish("memory:saved", {path = "/tmp/mem.md", tokens = 150})

-- Subscriber extension
rig.subscribe("memory:*", function(topic, data)
    rig.print("[memory] " .. topic .. " at " .. data.path)
end)
```

Wildcard patterns match topic prefixes.

## JSON

A `json` global is available:

```lua
local str = json.encode({key = "value"})   -- '{"key":"value"}'
local tbl = json.decode('{"key":"value"}') -- {key = "value"}
```

---

## Sandbox Restrictions

Extensions run in a **sandboxed** Lua environment. The following globals are **removed**:

| Removed | Why | Use instead |
|---------|-----|-------------|
| `os` | No direct OS access | `rig.exec(cmd)` |
| `io` | No file I/O | `rig.read_file()` / `rig.write_file()` |
| `loadfile` | No arbitrary code loading | - |
| `dofile` | No arbitrary code loading | - |
| `debug` | No debug introspection | - |

This means **you cannot use**:
- `os.tmpname()`, `os.remove()`, `os.execute()`
- `io.open()`, `io.read()`, `io.write()`
- `require()` for C modules (`package.cpath` is empty)

### Reading/writing files

Native file I/O (no shell overhead, handles binary safely):

```lua
-- read (returns nil + error on failure, 10MB limit)
local content, err = rig.read_file("/path/to/file")
if not content then rig.print("error: " .. err, {error=true}) end

-- write (returns boolean)
local ok = rig.write_file("/path/to/file", content)
```

Shell-based alternative (for pipes, globs, etc.):

```lua
local r = rig.exec("cat /path/to/file")
if r.ok then local contents = r.stdout end
```

### Clipboard

`xclip` forks a background process that holds pipes open, which makes `rig.exec()` hang.
**Always** redirect output and background it:

```lua
-- WRONG - will hang for 30 seconds:
rig.exec("echo 'text' | xclip -selection clipboard")

-- RIGHT:
rig.exec("echo 'text' | xclip -selection clipboard >/dev/null 2>&1 &")
```

This applies to any command that forks a persistent background process.

## `rig.exec()` Details

- Runs via `/bin/sh -c "your command"` - shell features (pipes, redirects, `&&`) work
- Default timeout: **30 seconds**
- Override with `rig.exec(cmd, {timeout = 60000})` (milliseconds)
- Returns `{ok, stdout, exit_code, timed_out}`
- `stdout` contains **both stdout and stderr** merged together
- The child process has **no stdin** - it cannot read interactive input

### Common pitfalls

```lua
-- WRONG - hangs, command waits for stdin:
rig.exec("cat")

-- WRONG - hangs, background process holds pipes:
rig.exec("some-daemon start")

-- RIGHT - detach background processes:
rig.exec("some-daemon start >/dev/null 2>&1 &")
```

## Error Handling

Lua errors in commands and hooks are shown in the TUI as `[ext] command error: ...`.
Wrap risky code in `pcall` if you want to handle errors gracefully:

```lua
rig.set("commands", "risky", function(args)
    local ok, err = pcall(function()
        -- code that might fail
    end)
    if not ok then
        rig.print("failed: " .. tostring(err), {error = true})
    end
end)
```

## Complete Examples

### Copy to clipboard

```lua
local b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local function b64encode(data)
    local out = {}
    for i = 1, #data, 3 do
        local a, b, c = data:byte(i, i + 2)
        b = b or 0; c = c or 0
        local n = a * 65536 + b * 256 + c
        local rem = #data - i + 1
        out[#out+1] = b64:sub(math.floor(n/262144)%64+1, math.floor(n/262144)%64+1)
        out[#out+1] = b64:sub(math.floor(n/4096)%64+1, math.floor(n/4096)%64+1)
        out[#out+1] = rem > 1 and b64:sub(math.floor(n/64)%64+1, math.floor(n/64)%64+1) or "="
        out[#out+1] = rem > 2 and b64:sub(n%64+1, n%64+1) or "="
    end
    return table.concat(out)
end

rig.set("commands", "copy", function(args)
    local msgs = rig.get("messages")
    if not msgs or #msgs == 0 then
        rig.print("no messages yet"); return
    end
    local text
    for i = #msgs, 1, -1 do
        if msgs[i].role == "assistant" and msgs[i].content ~= "" then
            text = msgs[i].content; break
        end
    end
    if not text then
        rig.print("no assistant response to copy"); return
    end
    local enc = b64encode(text)
    rig.exec("echo '" .. enc .. "' | base64 -d | xclip -selection clipboard >/dev/null 2>&1 &")
    rig.print("copied " .. #text .. " chars to clipboard")
end)
```

### Word count tool

```lua
rig.set("tools", "wc", {
    description = "Count words in a file",
    params = {path = {type = "string", description = "File path"}},
    run = function(p)
        local r = rig.exec("wc -w < '" .. p.path .. "'")
        if r.ok then return r.stdout
        else return "error: " .. r.stdout end
    end
})
```

### Auto-inject project context

```lua
rig.hook("tool_call", function(event, data)
    return true - -- allow all tool calls
end)

-- add project info to every conversation
local r = rig.exec("cat README.md 2>/dev/null | head -20")
if r.ok and r.stdout ~= "" then
    rig.set("prompts", "project_context",
        "Project README (first 20 lines):\n" .. r.stdout)
end
```
