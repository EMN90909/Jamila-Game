# Jamila-GD quick source audit and native migration plan

**Baseline read on 2026-10-18:** `EMN90909/Jamila-GD` default branch `Main`, source tree `f88ac1768e9ea1662a22468b0a09f6df1441c70e` (827 tracked blobs, 1,030 tree entries). The destination `EMN90909/Jamila-Game` had no commits on inspection. This is a static architecture audit, **not** a full security scan, dependency install or exhaustive line-by-line review.

## Findings

| Area | Evidence | Port implications |
|---|---|---|
| Host integration | `src/extension.ts`, `src/hosts/vscode/*`, `src/core/README.md` | VS Code events, commands, editor diffs and context tracking depend on the editor runtime; a standalone native app must reimplement them, not translate imports. |
| Task orchestration | `src/core/controller/index.ts` (~1,091 lines), `src/core/task/*`, `src/core/tools/*` | Separate controller, persistence, approvals, tool orchestration, cancellation, errors and request limits. |
| Provider APIs | `src/api/providers/*` (40+ source files) | Common OpenAI-compatible HTTP implemented; native Anthropic/Gemini/Bedrock/OpenRouter-specific adapters are not yet ported. |
| Tool security | `src/core/tools/bashTool.ts`, `readTool.ts`, `writeTool.ts` | Upstream tool schemas can request arbitrary commands and overwrite files. Do not grant unreviewed auto-execution. Native first step requires explicit approval, workspace checks and argument-vector execution. |
| MCP | `src/services/mcp/McpHub.ts` (~1,092 lines), `proto/mcp.proto` | HTTP/SSE/stdio discovery, trust and permissions are nontrivial; no MCP capability has been ported. |
| Web/browser | `src/services/browser/*`, `src/core/tools/browserActionTool.ts` | Chrome/CDP integration not available in this first native milestone. |
| UI | `webview-ui/src/App.tsx`, `webview-ui/src/components/*`, CSS/Tailwind | Original screens include chat, settings, history, account and MCP. Current native X11 GUI implements just chat/new-task with specified theme. |
| Protocols | `proto/*.proto`, `src/shared/*` | Reuse documented contracts later via generated native protobuf types if feasible; not currently equivalent. |
| Brand/source hygiene | `README.md`, `package.json`, `src/core/tools/bashTool.ts` | Visible legacy names (`claude-dev`, old URLs and instructions) remain upstream. Port should keep a clean Jamila identity and attribute licensing. |
| Noqeri readiness | `EMN90909/Noqeri/README.md` | Upstream 0.7.0 is explicitly unqualified; CLI provides `check`, not a stable native execution ABI. Noqeri file is syntax/check integration only. |

## Native implementation statuses

| Capability | Status |
|---|---|
| Native chat over OpenAI-compatible API | Implemented, local mock test |
| Workspace read/write tools (manual, approved) | Implemented, local tests |
| Process tool (manual approval, no shell) | Implemented, local tests |
| X11 graphical chat, black/paper-cream palette | Implemented, headless X display launch smoke tested |
| Zig FNV-1a statically linked helper | Implemented conditionally; Zig compiler unavailable in local test environment |
| Noqeri `.nqr` syntax checker launcher | Implemented; upstream compiler unavailable locally; Noqeri code not executed |
| Persistent task history, model cost/tokens, diff view | Not ported |
| Filesystem race-resistant isolation (dirfd/openat2) | Not hardened; current canonical-path checks do not prevent hostile concurrent symlink changes |
| Streaming, retry/backoff, cancellation and timeouts for subprocesses | Not ported |
| Native multi-provider adapters | Not ported |
| MCP resource/tool server integrations | Not ported |
| Browser automation/screenshots | Not ported |
| Production-quality Unicode text, accessibility, Windows/macOS GUI | Not ported |

## Next parity milestones

1. Replace proof-of-concept Xlib UI with a properly accessible native widget architecture. Add screenshots, keyboard focus, high-DPI scaling, text layout, errors, config and history pages.
2. Add provider adapter contract and mocks for supported APIs; stream responses, sanitize content and implement cancellation/rate-limit handling.
3. Implement robust tool-runner security using OS-level confinement, approval tokens and file diffs/rollback. Never allow model-provided commands to bypass approvals.
4. Add persisted task transcripts/checkpoints, token/cost counters, prompt budgets and feature parity regression tests.
5. Port MCP client with strict permission boundaries and browser integration; support editor plugins through an optional native host interface.
6. Revisit Noqeri runtime bindings **only after** upstream ABI/compiler tests pass, and qualify Zig builds in CI with a pinned known-good toolchain.

## Testing notes

`./build.sh` builds C++ and runs CTest. It does not automatically test live LLM providers; tests use a loopback mock. GUI display startup was manually smoke-tested under Xvfb, not a UI interaction test. License compatibility must be reviewed if upstream code is copied verbatim in future migrations.
