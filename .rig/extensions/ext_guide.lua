-- Inject a system prompt hint so the AI reads extensions.md before writing extensions

local cwd = rig.get("config", "cwd") or "."

rig.set("prompts", "extension_guide",
    "When the user asks you to create, modify, debug, or help with a Rig extension " ..
    "(Lua files in .rig/extensions/), you MUST first read " .. cwd .. "/docs/extensions.md " ..
    "before writing any code. This file documents the sandbox restrictions, API, and " ..
    "common pitfalls. Do not skip this step.")
