# Theme System

The pi-c theme system provides customizable color schemes for the TUI using JSON configuration files.

## Theme Format

A theme JSON file consists of three main sections:

```json
{
  "name": "Theme Name",
  "vars": {
    "variable-name": "value"
  },
  "colors": {
    "token-name": "color-value"
  }
}
```

### Variables Section

The `vars` section defines reusable color variables that can be referenced in the `colors` section:

```json
"vars": {
  "primary": "39",
  "secondary": "242",
  "success": "#a6e22e",
  "error": "196"
}
```

Variables can use any of the color formats described below.

### Colors Section

The `colors` section maps 51 semantic color tokens to actual color values:

```json
"colors": {
  "accent": "primary",
  "border": "242",
  "text": "#f8f8f2"
}
```

Color values can be:
- **Variable reference**: `"primary"` (references a var)
- **256-color**: `"39"` (xterm 256 color palette, 0-255)
- **RGB hex**: `"#ff0000"` (24-bit RGB color)
- **Default**: `""` (empty string for terminal default)

## Color Tokens

The theme system defines 51 semantic color tokens:

### Basic Colors
- `accent`, `border`, `success`, `error`, `muted`, `dim`, `text`

### Foreground Colors
- `fg.default`, `fg.muted`, `fg.subtle`, `fg.emphasis`
- `fg.success`, `fg.warning`, `fg.error`, `fg.info`
- `fg.primary`, `fg.secondary`

### Background Colors
- `bg.default`, `bg.subtle`, `bg.emphasis`, `bg.muted`, `bg.overlay`

### Border Colors
- `border.default`, `border.muted`, `border.emphasis`, `border.subtle`

### Status Colors
- `status.success`, `status.warning`, `status.error`, `status.info`

### Link Colors
- `link.default`, `link.hover`, `link.active`, `link.visited`

### Panel Colors
- `panel.bg`, `panel.border`, `panel.title`, `panel.subtitle`

### Input Colors
- `input.bg`, `input.border`, `input.text`, `input.placeholder`

### Button Colors
- `button.bg`, `button.text`, `button.border`, `button.hover`

### Badge Colors
- `badge.bg`, `badge.text`, `badge.border`

### Code Colors
- `code.bg`, `code.text`, `code.border`

## Example Themes

### Dark Theme (256-color)
```json
{
  "name": "Dark",
  "vars": {
    "primary": "39",
    "text": "252",
    "success": "34",
    "error": "196"
  },
  "colors": {
    "accent": "primary",
    "text": "text",
    "fg.success": "success",
    "fg.error": "error"
  }
}
```

### Monokai Theme (RGB)
```json
{
  "name": "Monokai",
  "vars": {
    "primary": "#66d9ef",
    "text": "#f8f8f2",
    "success": "#a6e22e",
    "error": "#f92672",
    "bg": "#272822"
  },
  "colors": {
    "accent": "primary",
    "text": "text",
    "bg.default": "",
    "bg.overlay": "bg"
  }
}
```

## API Usage

```c
#include "harness/themes.h"

// Load default theme
Theme *theme = theme_load_default();

// Load custom theme
Theme *custom = theme_load("path/to/theme.json");

// Resolve color token to ANSI escape code
char *color_code = theme_resolve_color(theme, "accent");
printf("%sAccented text\033[0m\n", color_code);
free(color_code);

// Discover themes in directories
const char *paths[] = {"~/.pi/themes", "/usr/share/pi/themes"};
int count = 0;
char **themes = themes_discover(paths, 2, &count);
for (int i = 0; i < count; i++) {
    printf("Found theme: %s\n", themes[i]);
    free(themes[i]);
}
free(themes);

// Clean up
theme_free(theme);
theme_free(custom);
```

## Color Reference

### 256-Color Palette
See [256-color lookup table](https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit) for available colors.

Common colors:
- Black: 0, Dark Gray: 8, Light Gray: 7, White: 15
- Red: 1, Green: 2, Yellow: 3, Blue: 4, Magenta: 5, Cyan: 6
- Bright variants: 9-14

### RGB Hex Format
Standard hex color notation: `#RRGGBB`
- Red: `#ff0000`
- Green: `#00ff00`
- Blue: `#0000ff`
- White: `#ffffff`
- Black: `#000000`
