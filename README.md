# Jamila Game · Godot Development Agent

**Native C++17 desktop + a real Godot MCP tool-calling loop.** Jamila Game focuses on Godot project exploration, scenes, scripts, debugging, and controlled edits/playtests. The UI uses ink-black `#11110F` and paper-cream `#F4EEDF`. This is a functional **v0.3 native foundation**, not a complete port of the original VS Code-based Jamila-GD.

## New in v0.3: Custom MCPs and approval notifications

- **Multi-server native host:** Connect independent Godot, GitHub and custom local stdio MCP servers. The model sees each server's advertised tools. Tool names are namespaced (`godot__...`, `github__...`) when multiple servers connect.
- **Approval notifications:** In Human Approval mode the AI pauses at an actionable in-app notification with **OK** (continue), **Review** (inspect arguments and approve/deny) and **Deny**. X11 raises the window and rings its bell.
- **Settings -> Execution Policy:** Switch between **Human Approval** (default) and **Allow All** (skip per-call permission for connected servers). An additional confirmation is required to enable Allow All in the GUI. The choice persists, with no secrets written to settings.
- **Custom MCP settings:** `--mcp-config /path/to/trusted/servers.json` accepts `mcpServers`, `command`, `args`, `disabled`, `${ENV_NAME}` environment references, and optional `includeTools` filter. Server failures are isolated; no remote server is started without explicit configuration.
- **Tests:** Added multi-server routing, configuration validation, approval persistence and Godot path-guard regression coverage.

See [MCP host and approvals](docs/MCP_HOST_AND_APPROVALS.md) and [example config](config/mcp-servers.example.json). Only **local stdio MCP** is currently supported, not remote Streamable HTTP/OAuth MCP. Connecting an MCP tool server does not itself provide ChatGPT/Claude model access; the reasoning model still needs a compatible API connection.

```sh
cp config/mcp-servers.example.json /tmp/my-servers.json
export JAMILA_API_KEY='your-provider-key'
./build/jamila-game --workspace /path/to/game --mcp-config /tmp/my-servers.json --gui
# CLI: /settings, /approval human, /approval allow-all, /tools, /doctor
```

## What changed in v0.2

- **MCP stdio client in native C++:** launches a configured server without a shell, negotiates MCP `initialize`, sends `notifications/initialized`, paginates `tools/list`, calls `tools/call`, handles JSON-RPC errors, timeouts, and response limits.
- **Game-dev agent loop:** discovers live MCP tool JSON Schemas and supplies them as OpenAI-compatible `tools`. Supports iterative model tool calls with correlated results (8 rounds maximum, 8 calls per round).
- **Godot-first instructions:** inspect project/scene tree, modify GDScript and scenes, run/playtest, read errors, report results. The agent uses the **actual tools advertised by the connected Godot server**, not imagined integrations.
- **Human review:** mutating or executable MCP calls must be approved in the CLI (`YES`) or X11 popup (`Y`/`N`). Headless `--ask` mode **denies** such calls. Known read-only Godot inspection tools may run without a prompt.
- **Improved X11 desktop:** Godot connection status, tool count, quick prompts for inspect/debug/scene/playtest, recent tool activity and native approval dialog.
- **Workspace guard:** blocks recognized MCP path arguments that escape the selected workspace, including `res://..` and absolute targets outside the project. This is defense-in-depth, **not an OS sandbox**.
- **Hermetic tests:** real native MCP subprocess + mocked OpenAI-compatible tool-call exchange. No API key/Godot/npm required for CI.

### Build on Linux (Debian / antiX / Ubuntu)

```sh
sudo apt install cmake g++ pkg-config libcurl4-openssl-dev libjson-c-dev libx11-dev
./build.sh
```

The **application** is native C++17. Zig is an optional native fingerprint module; its compiler is not required. Noqeri is currently restricted to explicit `noqeri check` calls because the upstream native execution toolchain is not qualified. The separately installed **Godot MCP server** may use Node.js or Python; Jamila does not bundle it or its source.

### Connect Godot MCP and start the agent

Install [Godot 4](https://godotengine.org/download/) and [Node.js 18+](https://nodejs.org/) for the default MCP server. You need a project directory containing `project.godot`.

```sh
export JAMILA_API_KEY='your-provider-api-key'
export JAMILA_MODEL='a-tool-calling-model-name'
# OpenAI-compatible endpoint is configurable. A local model can use an explicit
# loopback HTTP URL and does not require an API key.
# export JAMILA_API_URL='http://127.0.0.1:1234/v1/chat/completions'
export GODOT_PATH='/absolute/path/to/godot'    # only if Godot is not on PATH
./build/jamila-game --workspace /absolute/path/to/my-godot-project --godot-mcp --gui
```

The `--godot-mcp` flag **explicitly starts** `npx -y @coding-solo/godot-mcp@0.1.1`; npx may download that third-party server the first time. Use only trusted packages. To provide a locally installed MCP server instead (including an alternative live-editor integration):

```sh
./build/jamila-game --workspace /path/to/godot-project \
  --mcp-bin node --mcp-arg /absolute/path/to/godot-mcp/build/index.js --gui
```

MCP server adapters that use **stdio MCP** should work through the same transport, provided the connected server accepts standard protocol negotiation and OpenAI-compatible function schemas. No Streamable HTTP MCP transport is implemented yet. Servers with hundreds of tools may exceed the current 128-tool catalog cap; choose a smaller configured toolset.

### CLI for debugging

```sh
./build/jamila-game --workspace /path/to/game --godot-mcp
# /doctor           Godot project + server status
# /tools            Tools actually advertised by your server
# /tool get_godot_version {}
# /activity         Exact approvals / tool results
# /new              Clear conversation
# /read scenes/player.tscn
# /check-nqr policies/workspace.nqr
```

For regular natural language prompts: *“Inspect my player scene, find why jumping is broken, propose a repair and test it with Godot.”* The model must support OpenAI-compatible tool/function calling and must be able to access an API endpoint. A successful MCP connection alone does **not** imply a model is configured. You can continue to use chat without MCP, but that is advice-only mode.

## Safety and known limitations

**Trust boundary:** connected MCP servers execute as local programs with your account's privileges. The workspace path checks cover common argument names but cannot sandbox arbitrary tools, injected project scripts, plugins, or child processes. Work on a Git branch and inspect changes. Approval is not a security boundary against a malicious MCP server. In particular, Godot scripts launched by a tool may execute arbitrary code.

No unattended shell access is granted to the model. Existing CLI `/run` remains manual and requires confirmation. The native client protects MCP timeouts, message size, and tool catalog limits; it does **not** yet implement container isolation, undo/checkpoints, full native editor integration, token accounting, streaming replies, persisted history, image-aware tool results, OpenAI Responses API, or native Windows/macOS GUI. It also does not run the Godot engine in CI. The Godot MCP integration was verified against a hermetic protocol mock, **not** against a real running Godot editor.

Godot MCP options researched in October 2026:

- [Coding-Solo/godot-mcp](https://github.com/Coding-Solo/godot-mcp): lightweight stdio server, selected as the default target. MIT licensed; supports project launch, scenes, nodes, debug output.
- [hybridindie/godot-mcp](https://github.com/hybridindie/godot-mcp): advanced live-editor bridge, inspector, testing, debugger toolsets. Requires its own plugin/setup, verify compatible tool catalog before use.
- [Vollkorn-Games/godot-mcp](https://github.com/Vollkorn-Games/godot-mcp): additional scene/script/runtime tools and playtesting controls.

See [Godot agent implementation and verification](docs/GODOT_AGENT.md) and [source audit/parity](docs/AUDIT_AND_PARITY.md).

## Licensing and origins

Jamila-GD baseline (branch `Main`, tree `f88ac1768e9ea1662a22468b0a09f6df1441c70e`) declares Apache-2.0; Noqeri currently declares GPL-3.0-only. This project has not vendored either upstream source tree. The optional Zig/Noqeri units are not evidence of a fully native Noqeri application runtime. Review licensing when integrating upstream code or distributing externally maintained MCP servers.