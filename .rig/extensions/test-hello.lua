-- test extension: register a /hello command and a tool_call hook
rig.set("commands", "hello", function(args)
    local model = rig.get("config", "model")
    local cwd = rig.get("config", "cwd")
    local msg_count = rig.get("messages", "count")
    local tools = rig.get("tools")

    rig.print("hello from lua extension!")
    rig.print("model: " .. (model or "none"))
    rig.print("cwd: " .. (cwd or "none"))
    rig.print("messages: " .. tostring(msg_count))
    rig.print("tools: " .. tostring(#tools))

    -- test exec
    local r = rig.exec("echo 'lua exec works'")
    if r.ok then
        rig.print("exec: " .. r.stdout)
    else
        rig.print("exec failed: " .. tostring(r.exit_code), {error = true})
    end
end)

-- register a hook that logs tool calls
local hook_handle = rig.hook("tool_call", function(event, data)
    -- just observe, don't block
    return true
end)

-- register a custom tool
rig.set("tools", "lua_echo", {
    description = "Echo back the input (test tool from Lua)",
    params = {text = {type = "string", description = "text to echo"}},
    run = function(p)
        return "echo: " .. (p.text or p or "nil")
    end
})

rig.print("test-hello extension loaded")
