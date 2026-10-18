# Jamila Game v0.3: multi-server MCP and approval controls

## What is actually supported

Jamila **is the MCP host**. It runs each explicitly configured local **stdio** MCP server in its own subprocess, with a dedicated client. The host merges discovered `tools/list` schemas and passes them to the configured OpenAI-compatible tool-calling model. When there are 2+ connected servers, names are prefixed (`godot__create_scene`, `github__get_file_contents`, etc.) and translated back to the original tool name before dispatch. Schemas and permissions belong to the actual server, not to the tool prefix.

**Godot MCP cannot normally see or invoke other MCPs connected to an unrelated host.** The standard intentionally isolates servers: Jamila coordinates the calls; Godot MCP does not give Jamila an automatic tunnel to your Claude Desktop, VS Code, or editor-side MCP catalog. If another server itself advertises a bridge tool, Jamila can call it like any other tool, but it does not recursively discover arbitrary servers behind it.

**External APIs:** A connected GitHub MCP server can expose GitHub API operations. A custom MCP can expose supported Claude, OpenAI, or other API actions if it implements those tools. An MCP connection is *not* an API credential and does not automatically add a ChatGPT/Claude model adapter. The agent's reasoning model still uses an OpenAI-compatible chat-completions endpoint via `JAMILA_API_URL` / `JAMILA_MODEL` / `JAMILA_API_KEY`.

## Quick setup

Copy [config/mcp-servers.example.json](../config/mcp-servers.example.json) to a **local, untracked** file, then explicitly enable the servers you trust. Godot needs Godot 4 and Node.js 18+; the official GitHub MCP container needs Docker plus a narrowly scoped GitHub token. To use GitHub, set `GITHUB_PERSONAL_ACCESS_TOKEN` in your shell before starting Jamila and set `"disabled": false` for its entry. Only use GitHub access you deliberately authorize.

```bash
export JAMILA_API_KEY='your-model-provider-key'
export JAMILA_MODEL='a-tool-calling-model'
export GITHUB_PERSONAL_ACCESS_TOKEN='token-with-minimum-required-permissions'
./build/jamila-game --workspace /path/to/godot/project \
  --mcp-config /path/to/trusted/mcp-servers.json --gui
```

Or continue to use `--godot-mcp` for the default Godot-only configuration. `--mcp-bin EXECUTABLE --mcp-arg ARG` remains supported. You can combine `--godot-mcp` and `--mcp-config`, unless that config already contains a `godot` server. `/tools` lists the names actually exposed; `/doctor` shows connection errors. A server failure does not disconnect other healthy servers. A model can only access tools advertised by the connected servers and included in the host's catalog.

**Custom server format:** `{"mcpServers":{"mytools":{"command":"/absolute/path/to/mcp-binary","args":["stdio"],"env":{"TOKEN":"${MY_TOKEN}"},"includeTools":["read_file","search"]}}}`. The `env` map uses only `${VARIABLE}` references, evaluated from the launching shell; plaintext secrets in that map are rejected. `includeTools` filters visible MCP tools and helps meet the 128-tool limit imposed by the model function-calling interface. Omitting `includeTools` exposes all advertised tools. Set `"disabled": true` to disable a server.

## Approvals and actionable notification

**Settings -> Execution Policy** in the X11 desktop offers:

- **Human Approval (default):** A mutating or unknown MCP tool request *pauses the agent* and shows an in-app actionable notification with **OK** (approve and continue), **Review** (inspect tool and arguments, then approve/deny), and **Deny**. The application window is raised and rings the X11 bell. Denial returns a structured tool error so the model can recover. A small allowlist of known Godot inspection tools runs without a prompt. CLI requests use an exact `YES` response.
- **Allow All:** After a second confirmation in Settings, connected MCP tools run without per-call permission. This persists to `~/.config/jamila-game/settings.json` (or `$XDG_CONFIG_HOME/jamila-game/settings.json`) with owner-only permissions and no API secrets. Restore Human Approval at any time. `--approval-mode human|allow-all` is a *per-process override*. CLI `/approval human|allow-all` changes the persistent mode and asks for `YES` before enabling Allow All.

In-app actionable notifications are *not* OS-level desktop notification buttons. They require the Jamila X11 window to be open. In headless `--ask`, Human Approval rejects permissioned tools instead of auto-approving. Manually typed CLI `/run` and `/write` still require their own explicit confirmation.

## Security and limitations

- A local MCP server is a process with **the user's OS privileges**. Starting an untrusted server can already run arbitrary code before an approval is requested. Review installed packages, local binary paths, Docker images and token scopes before enabling a server. Do not enable Allow All with untrusted servers.
- Godot tool path arguments using recognized path names are checked against the workspace **in both modes**. This is defense-in-depth, not OS isolation. Non-Godot tools may legitimately use paths or URLs that are unrelated to the game project, so Jamila does not impose Godot path rules on them.
- Supports local *stdio* MCP only. **Streamable HTTP/OAuth MCP, MCP resources and prompts, server-initiated sampling, persistent tool-call transcripts, and OS-wide actionable notifications are not implemented**. The host starts explicitly configured commands, initializes their MCP sessions, calls `tools/list`, and dispatches `tools/call`.
- Limits: 12 configured servers, 128 exposed tools, 8 model tool rounds, 8 tool calls per round. Use `includeTools` or server-specific toolsets for huge catalogs. Tool messages and descriptions remain untrusted, and must never bypass host approval.
- Connecting an MCP server does not configure a language model. The agent needs an OpenAI-compatible chat endpoint and appropriate key. Claude's native Messages API is not currently supported as a model backend.

## Verification

Linux release build, existing core tests, and new hermetic two-server tests verify routing, approvals, persistent settings, Godot path checks in Allow All, invalid config handling, and namespaced tool lists. **Actual Godot editor automation, a real GitHub API session, Claude API calls, adversarial OS sandboxing and OS-level notification integrations were not tested.**

References: [MCP architecture](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/docs/specification/2025-11-25/architecture/index.mdx), [Coding-Solo Godot MCP](https://github.com/Coding-Solo/godot-mcp), [GitHub official MCP](https://github.com/github/github-mcp-server).