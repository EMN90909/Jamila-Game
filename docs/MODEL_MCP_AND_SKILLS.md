# Model, MCP and Skills setup for Jamila v0.4

## System-wide approval notifications (Linux X11)

Jamila sends a Freedesktop Desktop Notifications D-Bus request with three action buttons: **OK**, **Review**, **Deny**. The app listens to `org.freedesktop.Notifications.ActionInvoked`. Clicking OK accepts the *pending* action. Review raises Jamila for an argument inspection before a final choice. Deny rejects it. The app's in-window approval UI remains available and is the fallback if no notification daemon or `actions` capability exists.

Install an active desktop notification daemon (`xfce4-notifyd`, `dunst` or similar), `dbus-monitor`, and either `busctl` or `gdbus`. In antiX, `gdbus` can be used without systemd. Ensure `DBUS_SESSION_BUS_ADDRESS` is set and the notification daemon supports `actions`. The notification includes only the MCP tool name, never its JSON arguments or secrets. Notifications cannot authorize an action without a matching callback; a dismissed notification leaves Jamila awaiting a decision. This is an integration into the current X11 GUI, not a Wayland/macOS/Windows port.

## Model selection

An MCP server is a **tool provider**, not necessarily an AI inference endpoint. Jamila v0.4 has these model formats:

```sh
# OpenAI-compatible Chat Completions
export JAMILA_PROVIDER=openai
export JAMILA_API_KEY='YOUR_KEY'
export JAMILA_MODEL='YOUR_TOOL_CAPABLE_MODEL'

# Anthropic Claude Messages API
export JAMILA_PROVIDER=anthropic
export JAMILA_API_KEY='YOUR_ANTHROPIC_API_KEY'
export JAMILA_MODEL='YOUR_AVAILABLE_CLAUDE_MODEL'
# Automatically uses https://api.anthropic.com/v1/messages unless overridden.
```

`--provider anthropic` overrides the environment. The native adapter converts tool definitions and responses from OpenAI-style internal messages to/from Claude `tool_use`/`tool_result`. The Anthropic adapter is unit- and mock-end-to-end-tested, not yet authenticated against a real account. Never commit keys. Some OpenAI-compatible providers do not correctly support tool calling; select a model that does.

## Real Godot MCP setup

Install Godot 4 and Node.js 18+. Use the verified npm package `@coding-solo/godot-mcp@0.1.1` (MIT). It handles launching Godot, project inspection, scene/node edits, game starts/stops, and debug logs. It **does not guarantee** a complete general-purpose text editing tool; Jamila provides the complementary native workspace tools `jamila__list_files`, `jamila__read_file`, `jamila__write_file`.

```sh
export GODOT_PATH=/path/to/godot
./build/jamila-game --workspace /path/to/game --godot-mcp --doctor
./build/jamila-game --workspace /path/to/game --godot-mcp --team --gui
```

## Real GitHub MCP setup

Use the official `github/github-mcp-server` local stdio server. The example `config/mcp-servers.example.json` includes a disabled GitHub entry. To enable it, use Docker and provide `GITHUB_PERSONAL_ACCESS_TOKEN` in your environment, enabling only the least privileged toolsets and permissions needed. Then run:

```sh
./build/jamila-game --workspace /path/to/game --mcp-config /path/to/trusted/servers.json --doctor
```

An advertised GitHub tool is not proof that an authenticated API call will succeed. First use a read-only GitHub tool on an authorized repository, inspect returned data/errors, then (optionally) approve limited writes. Remote HTTP/OAuth MCP is still unimplemented; use a local stdio server.

## Local project Skills

Jamila discovers `SKILL.md` under these project-relative directories:

- `.agents/skills/<slug>/SKILL.md`
- `.jamila/skills/<slug>/SKILL.md`
- `.claude/skills/<slug>/SKILL.md`
- `skills/<slug>/SKILL.md`

Skill directories must be under the selected workspace and are read-only. Each file is capped at 16 KiB, up to 32 skills are loaded, and up to three skill documents matching the task/description are included as **untrusted reference content** in the model prompt. Skills cannot bypass approvals or execute commands themselves. Any referenced supporting files can be opened with the regular controlled file tools. Example: `examples/skills/godot-debugger/SKILL.md`; copy to your game's `.agents/skills/godot-debugger/SKILL.md`.

CLI: `/skills` lists discovered Skills.

## Specialist agents

Use `--team` or the GUI Settings toggle, or `/team on` in CLI. Three separate **read-only LLM adviser calls** are made concurrently: Planner, Researcher and Tester. Developer is the main tool-using agent and reads their summarized output. A Marketing Specialist adviser joins when the request is explicitly about publishing, marketing or launch. No specialist directly calls MCP or edits files; the Developer remains the only execution coordinator. This is bounded role-splitting, not independent unrestricted autonomous workers. Team mode means extra model requests and possible extra cost; it defaults off.