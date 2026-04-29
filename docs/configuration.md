# Configuration

## Directory Layout

Rig uses two configuration directories:

| Location | Purpose |
|----------|---------|
| `~/.rig/agent/` | Global config — settings, auth, models, sessions |
| `.rig/` | Project config — per-project settings, extensions, themes, prompts |

### Global Paths

| Path | Contents |
|------|----------|
| `~/.rig/agent/settings.json` | Global settings |
| `~/.rig/agent/auth.json` | Saved API credentials |
| `~/.rig/agent/models.json` | Custom model definitions |
| `~/.rig/agent/sessions/` | Saved conversation sessions |

### Project Paths

| Path | Contents |
|------|----------|
| `.rig/settings.json` | Project-specific settings |
| `.rig/extensions/` | Lua extensions, YAML workflows, shared libraries |
| `.rig/themes/` | Custom color themes |
| `.rig/prompts/` | Prompt templates |

Project root is detected by walking up from the current directory looking for a `.git` directory.

## Settings

Settings are layered with increasing priority:

1. **Built-in defaults** (lowest)
2. **Global** — `~/.rig/agent/settings.json`
3. **Project** — `.rig/settings.json`
4. **CLI flags** (highest)

Higher layers override lower ones. Setting a value to `null` in a higher layer removes it from the merged result.

### Default Values

```json
{
  "default_thinking_level": "medium",
  "theme": "default",
  "compaction": {
    "enabled": true
  },
  "retry": {
    "enabled": true,
    "max_retries": 3
  },
  "terminal": {
    "show_images": true
  },
  "snapshots": {
    "max_mb": 64,
    "file_max_mb": 1
  }
}
```

### Dot-Path Access

Settings support dot-notation for nested values:

```
compaction.enabled
retry.max_retries
terminal.show_images
snapshots.max_mb
```

Intermediate objects are created automatically when setting nested paths.

## Authentication

### Interactive Setup

```bash
rig auth          # choose provider, enter API key
rig auth status   # show current config
rig auth logout   # remove saved credentials
```

The interactive flow presents a menu of 9 providers:

1. Anthropic
2. OpenAI
3. Google
4. Mistral
5. AWS Bedrock
6. DeepSeek
7. xAI
8. Groq
9. OpenRouter

API keys are stored in `~/.rig/agent/auth.json` with restricted file permissions (0600).

### Environment Variables

Each provider checks specific environment variables (in priority order):

| Provider | Env Vars |
|----------|----------|
| Anthropic | `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_ARIG_KEY` |
| OpenAI | `OPENAI_ARIG_KEY` |
| Google | `GOOGLE_ARIG_KEY`, `GEMINI_ARIG_KEY` |
| Mistral | `MISTRAL_ARIG_KEY` |
| DeepSeek | `DEEPSEEK_ARIG_KEY` |
| xAI | `XAI_ARIG_KEY` |
| Groq | `GROQ_ARIG_KEY` |
| OpenRouter | `OPENROUTER_ARIG_KEY` |
| AWS Bedrock | `AWS_BEARER_TOKEN_BEDROCK`, `BEDROCK_ARIG_KEY`, `AWS_ACCESS_KEY_ID` |

Bedrock also supports IAM credentials (access key, secret key, region, session token) configured via `rig auth`.

## Permissions and Trust Rules

The permission system controls which tool calls the AI can execute without user approval.

### How It Works

When the AI requests a tool call:
1. Each trust rule is checked against the tool name and argument summary
2. If any rule matches, the call proceeds without asking
3. If no rule matches, the user sees an interactive prompt: "Allow this? [y/n]"

### Rule Format

Each rule has two parts:

| Field | Description |
|-------|-------------|
| `tool` | Tool name to match, or `"*"` for all tools |
| `pattern` | Glob pattern for arguments, or empty for "match all" |

Pattern matching uses glob syntax (`*`, `?`, `[...]`).

### Examples

| Rule | Effect |
|------|--------|
| `tool="bash", pattern=NULL` | Trust all bash commands |
| `tool="bash", pattern="git *"` | Trust bash commands starting with "git " |
| `tool="read", pattern=NULL` | Trust all file reads |
| `tool="write", pattern="/home/*"` | Trust writes under /home/ |
| `tool="*", pattern=NULL` | Trust everything (yolo mode) |

### Path Sandbox

File-access tools resolve paths through a sandbox that:
- Follows symbolic links to their real location
- Checks paths fall within allowed boundaries
- Prevents access outside the project directory
