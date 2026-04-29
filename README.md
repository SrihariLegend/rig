<p align="center">
  <img src="assets/screenshot.png" alt="Rig" width="700">
</p>

<p align="center">
  <img src="assets/hero.svg" alt="Why Rig" width="840">
</p>

## Tools

<p align="center">
  <img src="assets/tools.svg" alt="Tools" width="840">
</p>

## Providers

<p align="center">
  <img src="assets/providers.svg" alt="Providers" width="840">
</p>

## Terminal UI

A TUI with spatial lighting, not plain colored text:

- **Lantern rendering** - warm to cool color gradient that fades with distance from the cursor
- **Markdown rendering** - code blocks, bold, italic, lists, headings, rendered inline
- **Scrollback** - mouse wheel, Page Up/Down, vim keys
- **Themes** - JSON color schemes with hot reload via `/theme`
- **Responsive** - handles terminal resize, adapts to width

## Sessions

Conversations persist automatically. Resume with `rig --session <id>` or browse with `/sessions`.

## Modes

| Mode | Invocation | Use |
|------|-----------|-----|
| Interactive | `rig` | Full TUI |
| Print | `rig -p "prompt"` | One off, stdout |
| JSON | `rig --json -p "prompt"` | Structured output |
| RPC | Internal | Editor integration |

Works with pipes, redirects, and jq.

## Lua Extensions

<p align="center">
  <img src="assets/extensions.svg" alt="Lua Extensions" width="840">
</p>

Full documentation: [`docs/extensions.md`](docs/extensions.md)

## Architecture

<p align="center">
  <img src="assets/architecture.svg" alt="Architecture" width="840">
</p>

## Install

<p align="center">
  <img src="assets/install.svg" alt="Install" width="840">
</p>

Or build from source:

```bash
git clone https://github.com/SrihariLegend/rig.git
cd rig && make && sudo make install
```

## Docs

| | |
|---|---|
| [`extensions.md`](docs/extensions.md) | 8 Lua primitives, namespaces, sandbox |
| [`configuration.md`](docs/configuration.md) | Settings, permissions, trust rules |
| [`sessions.md`](docs/sessions.md) | Persistence, branching, context reconstruction |
| [`workflows.md`](docs/workflows.md) | YAML/JSON workflow engine, 16 step types |
| [`themes.md`](docs/themes.md) | Theme format, 51 color tokens |
| [`prompts.md`](docs/prompts.md) | Prompt templates, variable substitution |

## Contributing

```bash
make          # build
make test     # run tests
make clean    # clean
```

## License

MIT
