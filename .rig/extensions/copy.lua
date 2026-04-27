-- /copy — copy the last LLM response to the clipboard

-- base64 encode in pure Lua (no os/io available in sandbox)
local b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local function b64encode(data)
    local out = {}
    for i = 1, #data, 3 do
        local a, b, c = data:byte(i, i + 2)
        b = b or 0
        c = c or 0
        local n = a * 65536 + b * 256 + c
        local remaining = #data - i + 1
        out[#out + 1] = b64chars:sub(math.floor(n / 262144) % 64 + 1, math.floor(n / 262144) % 64 + 1)
        out[#out + 1] = b64chars:sub(math.floor(n / 4096) % 64 + 1, math.floor(n / 4096) % 64 + 1)
        out[#out + 1] = remaining > 1 and b64chars:sub(math.floor(n / 64) % 64 + 1, math.floor(n / 64) % 64 + 1) or "="
        out[#out + 1] = remaining > 2 and b64chars:sub(n % 64 + 1, n % 64 + 1) or "="
    end
    return table.concat(out)
end

rig.set("commands", "copy", function(args)
    local msgs = rig.get("messages")
    if not msgs or #msgs == 0 then
        rig.print("no messages yet")
        return
    end

    -- find the last assistant message
    local last_text = nil
    for i = #msgs, 1, -1 do
        if msgs[i].role == "assistant" and msgs[i].content ~= "" then
            last_text = msgs[i].content
            break
        end
    end

    if not last_text or last_text == "" then
        rig.print("no assistant response to copy")
        return
    end

    -- encode to base64 to avoid shell escaping issues
    local encoded = b64encode(last_text)

    -- close stdout/stderr/stdin so xclip's background process doesn't hold pipes open
    local cmd = "echo '" .. encoded .. "' | base64 -d | xclip -selection clipboard >/dev/null 2>&1 &"
    rig.exec(cmd)

    local lines = 0
    for _ in last_text:gmatch("\n") do lines = lines + 1 end
    rig.print("copied to clipboard (" .. tostring(#last_text) .. " chars, ~" .. tostring(lines) .. " lines)")
end)
