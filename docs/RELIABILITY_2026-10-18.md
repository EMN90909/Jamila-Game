# Jamila Game v0.4: verification and live-integration status

Date: 2026-10-18

## What was actually verified

| Gate | Observation | Interpretation |
|---|---|---|
| Linux native C++17 release build | PASS | Builds locally with GCC/CMake |
| MCP stdio initialize/list/call | PASS against child-process mock | Validates protocol flow, not compatibility with an installed upstream server |
| Multi-server tools, permission routing | PASS | Works with independent local mock processes |
| Desktop notification actions | PASS with mock `busctl`/`dbus-monitor` | System-daemon behavior not observed; GUI fallback remains |
| Native GDScript file edit/read/list | PASS | Creates directories, writes files atomically, reads back content and respects workspace guard |
| Human Approval versus Allow All | PASS | Headless Human mode denies writes; explicitly enabled Allow All permits them |
| Skills local discovery | PASS | Loads `SKILL.md` with bounded size; refuses symlinked/outside skill directories |
| Four-role team | PASS with mock LLM | Planner, Researcher, Tester concurrent advisers plus main Developer; optional Marketing on launch requests |
| OpenAI-compatible API tool loop | PASS against local HTTP mock | Does not prove real model behavior |
| Claude Messages tool loop | PASS against local HTTP mock | `tool_use` / `tool_result` conversion tested, real Claude credentials not supplied |
| Godot executable/edit/run/playtest | NOT VERIFIED | Godot binary absent in test container |
| Coding-Solo Godot MCP `@0.1.1` | NOT VERIFIED LIVE | Node exists but npm package not cached; container has no DNS access to npm/GitHub |
| Official GitHub MCP authenticated tools | NOT VERIFIED LIVE | Docker/server and GitHub PAT absent from test container |
| Real Claude API authenticated request | NOT VERIFIED LIVE | Anthropic API key and outbound network unavailable |
| Noqeri native runtime | NOT VERIFIED | Its upstream compiler native game tooling is not qualified |

The workflow test **does** complete script editing and MCP project inspection *when both the model and MCP server are simulated*. It **does not** validate GDScript parsing, running a Godot scene, gameplay behavior, visual output, or real API connectivity. It would be misleading to describe that test as a real Godot playtest.

## Runtime dependency diagnostics

Run after installing Godot and the MCP server in the same environment:

```sh
export GODOT_PATH=/absolute/path/to/godot
./build/jamila-game --workspace /absolute/path/to/project --godot-mcp --doctor
```

`--doctor` identifies whether `project.godot` exists, connected servers, exported tool counts and, when discovered, invokes the real `get_godot_version` MCP tool. Inspect that tool's `isError` field and then use `/tool` to run supported project-inspection tools. A working `get_godot_version` **still does not** prove that scene editing or game execution works.

Suggested manual live test sequence on a machine with Godot:

1. `godot --headless --version` and verify `project.godot` is present.
2. Connect `@coding-solo/godot-mcp@0.1.1`; confirm advertised names using `/tools`.
3. Call the version and project-inspection tools; check for `isError` and verify returned paths.
4. Start a new disposable Godot project, commit a clean baseline, and approve a simple scene creation via MCP.
5. Ask Jamila to add a GDScript file using `jamila__write_file` (or a server-native editor tool), reopen that file, then invoke the Godot run/debug tools.
6. Inspect Godot errors, scene hierarchy, and game output. Fix and rerun until engine errors are gone.
7. Test a denied approval, a failed tool call, and restoration from the Git baseline.

Use only trusted projects and MCP binaries. Godot imports, project scripts, and server processes execute under your user's privileges. Neither approval mode nor path validation is a sandbox.

## Outstanding gaps / next actions

- Run these steps on a real machine with Godot 4, the official GitHub MCP (with least-privilege PAT), and a real Claude API key. Store secrets outside repositories.
- Verify Debian antiX/desktop action support with `busctl` or `gdbus`; if the desktop notification server omits the `actions` capability, Jamila retains the in-app approval popup.
- Add proper diffs, checkpoints/undo, cancellation of long-running tools, and automated Godot headless builds with an actual engine binary.
- Strengthen MCP transport security against malicious local processes (MCP servers are currently unrestricted OS subprocesses).
- Extend skill parser beyond local top-level `SKILL.md` files and add reliable evaluation of skill effectiveness.
- Test real parallel-agent behavior against chosen hosted models; increased concurrent calls can increase token spending.