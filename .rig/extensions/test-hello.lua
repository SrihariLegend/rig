-- test extension: /hello command, tool_call hook, custom tool

-- register /hello slash command
rig.set("commands", "hello", function(args)
    local model = rig.get("config", "model")
    local cwd = rig.get("config", "cwd")
    local msg_count = rig.get("messages", "count")
    local tools = rig.get("tools")

    rig.print("hello from lua extension!")
    rig.print("  model: " .. (model or "none"))
    rig.print("  cwd: " .. (cwd or "none"))
    rig.print("  messages: " .. tostring(msg_count))
    rig.print("  tools: " .. tostring(#tools))

    -- test exec
    local r = rig.exec("date")
    if r.ok then
        rig.print("  date: " .. r.stdout)
    end
end)

-- register /count command — shows how many times tools have been called
tool_calls = 0
local h = rig.hook("tool_call", function(event, data)
    tool_calls = tool_calls + 1
    return true
end)

rig.set("commands", "count", function(args)
    rig.print("tool calls this session: " .. tostring(tool_calls))
end)

-- register a custom tool the LLM can use
rig.set("tools", "lua_echo", {
    description = "Echo back the input text (test tool from Lua extension)",
    params = {text = {type = "string", description = "text to echo back"}},
    run = function(p)
        return "lua echo: " .. (p.text or tostring(p) or "nil")
    end
})
