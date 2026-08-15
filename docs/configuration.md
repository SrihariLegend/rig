# Configuration

## Directory Layout

Rig uses two configuration directories:

| Location | Purpose |
|----------|---------|
| `~/.rig/agent/` | Global config - settings, auth, models, sessions |
| `.rig/` | Project config - per-project settings, extensions, themes, prompts |

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
2. **Global** - `~/.rig/agent/settings.json`
3. **Project** - `.rig/settings.json`
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

Rig's auth system is a port of pi's layered architecture: a multi-provider
credential store, a per-provider auth registry (API key and/or OAuth), and a
UI-agnostic login interaction contract.

### Commands

```bash
rig auth                              # interactive login (pick provider + method)
rig auth status                       # show stored + ambient credentials
rig auth check --provider <p> [--json] [--credentials] [--no-refresh]
rig auth print-api-key --provider <p> [--model <m>]
rig auth print-bearer-token --provider <p> [--min-expiry 30m]
rig auth logout [--provider <p>]      # logout one or all providers
```

`check` exits 0 (ready) / 1 (not_ready) / 2 (invalid). `print-api-key` and
`print-bearer-token` resolve and print a credential for scripting; OAuth bearer
tokens are refreshed when `--min-expiry` demands it. `--model` resolves the
provider via the model registry.

### Interactive login

`rig auth` lists every registered provider, marks which offer OAuth
(subscription) vs API key, and lets you pick. If a provider supports both, you
choose the method. After login, Rig discovers and prints the models available
for that provider (builtin models, or a note to add custom models via
`~/.rig/agent/models.json`).

### Credential storage

Credentials live in `~/.rig/agent/auth.json` (0600, parent dir 0700), keyed by
provider id, one credential per provider:

```json
{
  "anthropic": { "type": "oauth", "access": "...", "refresh": "...", "expires": 1730000000000 },
  "openai":    { "type": "api_key", "key": "sk-..." },
  "bedrock":   { "type": "api_key", "env": { "AWS_ACCESS_KEY_ID": "...", "AWS_SECRET_ACCESS_KEY": "...", "AWS_REGION": "us-east-1" } }
}
```

Writes are serialized per provider through a file lock (`auth.json.lock`).

### Resolution

A stored credential owns the provider: ambient env vars are consulted only when
nothing is stored. OAuth tokens refresh automatically under the store lock when
within 5 minutes of expiry (double-checked so concurrent requests don't
double-refresh). API-key credentials may carry a provider-scoped `env` map
(e.g. Cloudflare account id, Bedrock AWS keys).

### Environment variables (ambient fallback)

| Provider | Env Vars |
|----------|----------|
| Anthropic | `ANTHROPIC_AUTH_TOKEN` (Bearer), `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_API_KEY` |
| OpenAI | `OPENAI_API_KEY` |
| Google | `GEMINI_API_KEY` |
| Google Vertex | `GOOGLE_CLOUD_API_KEY` (or ADC) |
| DeepSeek | `DEEPSEEK_API_KEY` |
| Mistral | `MISTRAL_API_KEY` |
| xAI | `XAI_API_KEY` |
| Groq | `GROQ_API_KEY` |
| OpenRouter | `OPENROUTER_API_KEY` |
| AWS Bedrock | `AWS_PROFILE`, `AWS_ACCESS_KEY_ID`+`AWS_SECRET_ACCESS_KEY`, `AWS_BEARER_TOKEN_BEDROCK`, ECS/IRSA vars |
| GitHub Copilot | `COPILOT_GITHUB_TOKEN` |

### OAuth flows

Subscription/browser flows: Anthropic (Claude Pro/Max), OpenAI Codex
(ChatGPT), xAI (SuperGrok/X Premium), OpenRouter, Radius, Kimi Coding, GitHub
Copilot. PKCE-callback flows spin up a loopback HTTP server and race a manual
paste prompt for headless sessions; device-code flows poll per RFC 8628.

Bedrock also supports IAM credentials (access key, secret key, region, session
token) configured interactively via `rig auth`.

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
