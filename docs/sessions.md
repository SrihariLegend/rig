# Sessions

Conversations are automatically saved and can be resumed later.

## Basics

```bash
rig --session abc123    # resume a specific session
```

In interactive mode:
- `/session` - show current session info
- `/sessions` - browse and pick a previous session
- `/fork` - branch the conversation from a specific point

## Storage Format

Sessions are stored as JSONL files (one JSON object per line) in `~/.rig/agent/sessions/`.

### Filename Format

```
MODIFIED-CREATED-KEYWORD-HASH.jsonl
```

| Part | Format | Example |
|------|--------|---------|
| MODIFIED | `YYYYMMDDTHHmm` | `20260429T1430` |
| CREATED | `YYYYMMDDTHHmm` | `20260429T1400` |
| KEYWORD | Extracted from first user message | `refactor` |
| HASH | First 4 chars of session ID | `a3f2` |

## Entry Types

Each line in a session file is an entry with:
- A unique 16-character hex ID
- A parent ID (linking to the previous entry)
- A type
- A data payload (JSON)
- A timestamp (milliseconds)

| Type | String Key | Purpose |
|------|-----------|---------|
| Message | `message` | User, assistant, or tool result message |
| Thinking Level Change | `thinkingLevelChange` | Records when thinking level was changed |
| Model Change | `modelChange` | Records when the AI model was switched |
| Compaction | `compaction` | Marker that conversation was compacted |
| Branch Summary | `branchSummary` | Summary of an abandoned branch |
| Custom | `custom` | Extension-defined entry |
| Custom Message | `customMessage` | Extension-defined message |
| Label | `label` | User-defined bookmark |
| Session Info | `sessionInfo` | Session metadata (name) |

## Branching

Sessions form a linked chain via parent IDs. Branching works by moving the "leaf" pointer to a previous entry. New messages then chain from that point, creating a fork.

The old branch remains intact in the file - both paths coexist.

### Context Reconstruction

When loading a session, Rig walks backward from the current leaf through parent links to the root, then reverses to get chronological order. Only `message` entries are included in the conversation context sent to the AI.

This means switching branches instantly changes which messages the AI sees, without duplicating data.
