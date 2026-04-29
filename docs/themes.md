# Themes

Themes are JSON files that define color schemes for the TUI. Drop them in `.rig/themes/` or `~/.rig/agent/themes/` and switch with `/theme`.

## Format

```json
{
  "name": "My Theme",
  "vars": {
    "primary": "#66d9ef",
    "text": "#f8f8f2"
  },
  "colors": {
    "accent": "primary",
    "fg.default": "text"
  }
}
```

### Sections

| Section | Purpose |
|---------|---------|
| `name` | Display name |
| `vars` | Reusable color variables |
| `colors` | Maps 51 semantic tokens to colors |

### Color Formats

| Format | Example | Description |
|--------|---------|-------------|
| Variable ref | `"primary"` | References a var by name |
| 256-color | `"39"` | xterm 256-color palette index (0-255) |
| RGB hex | `"#ff0000"` | 24-bit truecolor |
| Empty | `""` | Terminal default |

## Color Tokens

### Foreground

`fg.default`, `fg.muted`, `fg.subtle`, `fg.emphasis`, `fg.success`, `fg.warning`, `fg.error`, `fg.info`, `fg.primary`, `fg.secondary`

### Background

`bg.default`, `bg.subtle`, `bg.emphasis`, `bg.muted`, `bg.overlay`

### Border

`border.default`, `border.muted`, `border.emphasis`, `border.subtle`

### Status

`status.success`, `status.warning`, `status.error`, `status.info`

### Links

`link.default`, `link.hover`, `link.active`, `link.visited`

### Panels

`panel.bg`, `panel.border`, `panel.title`, `panel.subtitle`

### Input

`input.bg`, `input.border`, `input.text`, `input.placeholder`

### Buttons

`button.bg`, `button.text`, `button.border`, `button.hover`

### Badges

`badge.bg`, `badge.text`, `badge.border`

### Code

`code.bg`, `code.text`, `code.border`

### Basic

`accent`, `border`, `success`, `error`, `muted`, `dim`, `text`

## Examples

See [`examples/themes/`](../examples/themes/) for complete theme files:
- `dark.json` — 256-color dark theme
- `light.json` — 256-color light theme
- `monokai.json` — RGB hex Monokai theme
