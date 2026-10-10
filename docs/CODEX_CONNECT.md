# Connect Codex to the open ARK

For Claude Code/Desktop, OpenCode and provider connections inside ARK, see
[AI connectors](AI_CONNECTORS.md). The live document bridge below is shared.

ARK 0.8 provides a local live-session bridge. Codex calls the existing ARK MCP
tools; Studio executes them on its document thread and refreshes the operator's
view. It uses the same document and revision as the operator. No model API key,
cloud relay or network listener is required by this bridge.

Open Studio's agent/connection settings and copy its active pipe endpoint and
MCP configuration. The endpoint identifies this Studio process and changes after
restarting Studio. Configure your MCP client explicitly for that endpoint:

```toml
[mcp_servers.ark]
command = 'C:\path\to\ARK-Point\out\native\Release\ark-mcp.exe'
args = ['--connect', 'ark-studio-REPLACE-WITH-ACTIVE-ENDPOINT']
```

Use the actual executable path from your build. TOML single-quoted strings retain
Windows backslashes. ARK does not modify your Codex configuration automatically.
Codex's MCP configuration is described in the
[official Codex MCP documentation](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
Restart/reconnect the MCP client after changing its endpoint configuration.

Call `agent_onboard` first. It describes the available modeling operations,
current document/revision and numerical limits. Read compact inspection tools and
use `doc_apply` with `expected_revision` to protect against intervening operator
edits. Successful changes appear in the same Studio window; they share undo
history. Saving/exporting still requires an explicit tool operation.

Without `--connect`, `ark-mcp` keeps its original standalone behavior: it owns an
independent document and does not change any Studio window. With `--connect`, a
missing/disconnected Studio is an explicit transport error. There is no silent
fallback to an empty standalone document.

## Transport and failure contract

The Windows named pipe allows the current Windows user only, rejects remote
clients, and is created exclusively. It accepts one request at a time. Request
payloads are limited to 4 MiB and responses to 32 MiB; prefer `doc_info`,
`sketch_inspect`, `source_inspect` and other compact tools over large geometry
payloads. Each connection has a 30-second limit and cancellable overlapped I/O.
Do not point untrusted clients at the bridge: its tools have the same local file
and document authority as the standalone ARK MCP host.

Stopping Studio cancels pending bridge work without waiting for its window event
loop. A queued callback checks cancellation before executing. Already running
geometry evaluation is not preempted. If a call times out or its connection is
lost after dispatch, its mutation may have completed: inspect the document and
revision before retrying. There is no automatic mutation replay.

`initialize`, `ping` and `tools/list` are answered by the MCP proxy locally;
`tools/call` is forwarded to Studio. A successful initialization alone does not
prove that the Studio endpoint is connected. Use `agent_onboard` or `doc_info` to
confirm the live document.

This bridge is the executable connection for an external Codex agent. It does not
itself embed an LLM or turn a text field into an authenticated model chat client.
