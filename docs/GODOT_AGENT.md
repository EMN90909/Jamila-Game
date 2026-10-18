# Native Godot MCP agent · implementation notes

## Architecture

```text
Native X11 UI / CLI
  -> Jamila Agent (C++17)
     -> OpenAI-compatible Chat Completions + tools[]
       <- assistant tool_calls[]
     -> Authorization boundary (read-only allowlist, human approval, path checks)
     -> Native MCP stdio transport (one child process)
         initialize -> notifications/initialized -> tools/list (+ cursor)
         -> tools/call -> tool results
       -> Installed Godot MCP server (external Node.js/Python process)
          -> Godot editor, headless executable, scenes and scripts
     -> Tool result messages (correlated tool_call_id) -> model -> user
```

**Important:** Jamila talks to the server via JSON-RPC MCP, *not* a homemade GDScript invocation interface. The server's real tool schemas are registered with the provider on each prompt. A model that does not support Chat Completions tools cannot use this mode.

## Security decisions

- MCP command is **opt-in**. No model-supplied executable argv. Child is spawned directly with `execvp` (no shell string).
- All MCP calls are matched to the advertised tool catalog. The known read-only names `get_godot_version`, `get_project_info`, `get_debug_output`, `list_projects`, `get_uid` skip confirmation. Other tools require review even when they appear observational.
- Absolute/relative and `res://` path arguments are checked against the canonical workspace where they match common path property names. `user://` and `file://` are rejected in those fields. Nested object/array arguments receive the same checks. This guard can miss names used by different servers.
- The model receives no API-key text and tool output is treated as untrusted context. The external MCP server still runs as the local user and inherits most environment variables. Use a trusted server and avoid loading arbitrary Godot projects/scripts.
- MCP responses limited to 2 MiB per frame; 128 tools max; discover pagination limited; tool/chat rounds bounded; provider HTTP body max 4 MiB. Tool results sent back to the model are truncated at 24 KiB.
- Unapproved/invalid actions cannot mutate through the native dispatcher. This does not secure server implementation vulnerabilities, or Godot imports/plugins invoked by approved server calls.
- The X11 UI approval popup shows the tool name and a preview of the JSON arguments. Read the project diff before merging generated changes.

## Tests

`./build.sh` compiles and runs CTest:

1. Core: filesystem traversal, symlink denial, size caps, manual process, FNV fingerprint, local chat mock.
2. MCP agent: stdio initialize, tool discovery, read-only tool call, denied action, explicitly approved action, invalid/escaping paths, unknown tool rejection.
3. Model integration: hermetic HTTP server returns a tool call; Jamila invokes MCP and returns the tool result to the same model session, which emits a final answer.
4. CLI help smoke test.
5. CI Linux/Xvfb window launch smoke (no live Godot engine).

**Not validated:** real Godot executable and real production MCP package; these are external dependencies and must be tested with a local game project and provider key. No screenshot analysis, interactive native accessibility test, or undo support is claimed.

## Follow-up milestones

1. Install an actual Godot 4.x editor, connect the pinned server, and run a sample `project.godot` end-to-end including a scene edit and a headless playtest.
2. Add safe diff inspection, checkpoints, undo, project revision tracking, multi-file transactional changes, and explicit permission scopes.
3. Add rich multimodal results (viewport screenshots, image capture), script diagnostics, inspector/scene hierarchy, and error/preview panels.
4. Provider retries/cancellation, streaming, token usage, plans, context retrieval, task persistence, tool filtering for very large servers.
5. Optional native Zig/Noqeri runtime integrations only when their compilers, C ABIs, and game SDK qualify with tests.
