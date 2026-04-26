# Prompt Templates

Prompt templates are Markdown files that can be expanded with arguments using `/name arg1 arg2` syntax.

## File Structure

Each prompt template is a `.md` file with optional YAML frontmatter:

```markdown
---
description: Brief description of the template
argument-hint: <arg1> <arg2>
---

Template content with $1, $2, $@, etc.
```

## Substitution Syntax

### Positional Arguments

- `$1` through `$9` - Individual arguments (1-indexed)
- Missing arguments are replaced with empty strings

Example:
```markdown
Hello $1, you are $2!
```
Invoked as `/greet Alice awesome` produces:
```
Hello Alice, you are awesome!
```

### All Arguments

- `$@` or `$ARGUMENTS` - All arguments joined with spaces

Example:
```markdown
Please explain: $@
```
Invoked as `/explain the meaning of life` produces:
```
Please explain: the meaning of life
```

### Range Syntax

- `${@:N}` - Arguments from Nth position to end (1-indexed)
- `${@:N:L}` - L arguments starting at position N

Examples:
```markdown
First: $1
Rest: ${@:2}
Middle 2: ${@:2:2}
```
Invoked as `/test A B C D` produces:
```
First: A
Rest: B C D
Middle 2: B C
```

## Discovery

Prompt templates are discovered from configured directories:
- Only `.md` files directly in the directory (non-recursive)
- Filename without `.md` extension becomes the template name
- First template found wins if duplicates exist across directories

## Frontmatter Fields

- `description` - Brief description shown in autocomplete
- `argument-hint` - Shown in autocomplete to guide usage (e.g., `<file> [options]`)

Both fields are optional. If omitted, the template still works but without metadata.

## Examples

### Simple Template

File: `greet.md`
```markdown
Hello $1!
```

Usage: `/greet World` → `Hello World!`

### With Frontmatter

File: `review.md`
```markdown
---
description: Review code for issues
argument-hint: <file-path>
---

Please review the code in $1 for:
- Bugs
- Performance
- Security
```

Usage: `/review src/main.c`

### Using All Arguments

File: `ask.md`
```markdown
---
description: Ask a question
argument-hint: <question>
---

Question: $@

Please provide a detailed answer.
```

Usage: `/ask what is the meaning of life`

## Best Practices

1. Use descriptive filenames (lowercase, hyphens for spaces)
2. Add frontmatter for clarity in autocomplete
3. Use `$1-$9` for specific required arguments
4. Use `$@` for free-form text arguments
5. Use `${@:N}` to skip initial arguments
6. Keep templates focused on a single purpose
