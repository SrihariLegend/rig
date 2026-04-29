# Prompt Templates

Prompt templates are Markdown files that expand with arguments when invoked as slash commands.

## Setup

Place `.md` files in `.rig/prompts/`:

```markdown
---
description: Explain a concept in detail
argument-hint: <concept>
---

Please explain $@.
```

The filename (without `.md`) becomes the command name. Invoke with `/explain quicksort`.

## Frontmatter

Optional YAML frontmatter between `---` markers:

| Field | Purpose |
|-------|---------|
| `description` | Shown in autocomplete |
| `argument-hint` | Usage hint shown alongside description |

## Substitution Syntax

### Positional Arguments

| Syntax | Meaning |
|--------|---------|
| `$1` | First argument |
| `$2` through `$9` | Nth argument |
| `$@` or `$ARGUMENTS` | All arguments joined with spaces |

Missing positional arguments become empty strings.

### Range Syntax

| Syntax | Meaning |
|--------|---------|
| `${@:N}` | Arguments from position N to end (1-indexed) |
| `${@:N:L}` | L arguments starting at position N |

### Example

Template (`review.md`):
```markdown
---
description: Review code for issues
argument-hint: <file-path> [focus-area]
---

Review the code in $1 focusing on:
${@:2}
```

Usage: `/review src/main.c security performance`

Expands to:
```
Review the code in src/main.c focusing on:
security performance
```

## Discovery

Templates are loaded from configured prompt directories. Only `.md` files directly in the directory are discovered (not recursive). First match wins if duplicates exist across directories.

## Examples

See [`examples/prompts/`](../examples/prompts/) for working templates:
- `explain.md` - Explain a concept in detail
- `review-code.md` - Code review with specific focus areas
- `summarize.md` - Concise summaries with length limits
