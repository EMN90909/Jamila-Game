# Jamila Game

A **native first milestone**, not a claimed 1:1 port of [Jamila-GD](https://github.com/EMN90909/Jamila-GD). This project begins a clean-room C++ / Zig / Noqeri migration of the original coding-assistant experience. No TypeScript, React or JavaScript is used in the application.

## Current capabilities

- C++17 chat session using an OpenAI-compatible HTTP API with configurable model, URL and API key.
- Native X11 graphical shell with a black (`#11110F`) and paper-cream (`#F4EEDF`) theme, chat input, conversation display and new-task action.
- CLI commands to read/write workspace files, execute a command with explicit approval, review the session history and check Noqeri source files.
- Workspace traversal/symlink path checks, file-size and response-size caps, HTTPS-only remote endpoints, no automatically executed LLM tools.
- Optional **real Zig C ABI** implementation of the FNV-1a fingerprint helper. C++ fallback is used when Zig is not installed.
- Experimental Noqeri policy source and explicit `noqeri check` integration. **Noqeri runtime execution is not supported** by the current upstream compiler.
- Unit and local HTTP mock integration tests.

## Linux build

Prerequisites: CMake 3.20+, a C++17 compiler, pkg-config, `libcurl` and `json-c` development headers. X11 development headers are needed for the GUI. Zig is optional (the C++ fallback is built without it).

On Debian/Ubuntu/antiX:

```bash
sudo apt install cmake g++ pkg-config libcurl4-openssl-dev libjson-c-dev libx11-dev
./build.sh
```

## Run

```bash
export JAMILA_API_KEY='your-provider-key'
export JAMILA_MODEL='gpt-4o-mini'
# Optional: export JAMILA_API_URL='https://your-provider.example/v1/chat/completions'
./build/jamila-game --gui
# Or CLI:
./build/jamila-game --workspace /path/to/project
# Or one message:
./build/jamila-game --ask 'Explain this project'
```

Local OpenAI-compatible providers can use `JAMILA_API_URL=http://127.0.0.1:PORT/v1/chat/completions` without a key. Never commit a real API key; the app reads it from the environment.

In the terminal UI, type `/help`, `/read src/main.cpp`, `/write notes.txt hello`, `/run /usr/bin/git status`, `/check-nqr policies/workspace.nqr`, or `/new`. Confirm potentially dangerous actions with the exact word `YES`. Arguments to `/run` are passed directly, **not through a shell**; no command-pipe or shell chaining is interpreted.

The native X11 UI is currently a lightweight prototype. Use Ctrl+Enter to send, mouse wheel to scroll, click New Task to clear chat, Esc to clear input. CLI and GUI do not share persistent history. No model response may write files or invoke commands automatically.

## Source audit and missing parity

See [docs/AUDIT_AND_PARITY.md](docs/AUDIT_AND_PARITY.md). Major upstream systems (VS Code integration, MCP, browser automation, multi-provider adapters, rich chat formatting, diff review/checkpoints and task history persistence) are **not implemented**. Calling this a complete replica would be inaccurate. C++17 core builds and local tests run on Linux; Zig and Noqeri toolchain checks require their respective installed compilers.

Source baseline: Jamila-GD branch `Main`, tree `f88ac1768e9ea1662a22468b0a09f6df1441c70e`, Apache-2.0 licensing. Noqeri branch `main`, tree `b925240ea387d35765de1ae9f056ae8f087d5900`, GPL-3.0-only. Zig and Noqeri are optional interfaces rather than vendored source.
