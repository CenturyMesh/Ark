# Connect Codex to the open ARK

For Grok Build, Claude Code/Desktop, OpenCode and provider connections inside ARK, see
[AI connectors](AI_CONNECTORS.md). The live document bridge below is shared.

ARK provides a local live-session bridge on Windows. For the account-versus-MCP
distinction and setup troubleshooting, start with [Connecting](CONNECTING.md).
Codex calls the existing ARK MCP
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

Since 045A every bridged call declares the client actor `{kind:"client",
id:"live_mcp"}`. Committed authoring and failed calls, including failed
`query_batch` rows and rejected previews, enter the project's
[activity ledger](AGENT_ACTIVITY.md) with their measured outcome; successful
reads are not recorded. Receipts then carry `activity_id` and
`pending_feedback_ids`; rate them with `tool_feedback`. The client's token
usage remains unknown to ARK unless the client reports it with `usage_record`.

Without `--connect`, `ark-mcp` keeps its original standalone behavior: it owns an
independent document and does not change any Studio window. With `--connect`, a
missing/disconnected Studio is an explicit transport error. There is no silent
fallback to an empty standalone document.

## Transport and failure contract

The Windows named pipe allows the current Windows user only, rejects remote
clients, and is created exclusively. Four instances accept connections, so a
further client can connect and queue while a long call runs; Studio still
executes one document call at a time. Request
payloads are limited to 4 MiB and responses to 32 MiB; prefer `doc_info`,
`sketch_inspect`, `source_inspect` and other compact tools over large geometry
payloads. Connecting and transferring each request or response have 30-second
windows with cancellable overlapped I/O; Studio then waits up to 10 minutes for
the tool result, because full recomputes of large models can exceed a minute.
A client that sends `_meta.progressToken` with `tools/call` receives MCP
`notifications/progress` about every two seconds while it waits: elapsed seconds
and, during a recompute, the feature being evaluated. Progress is display
information only; the single tool result remains the outcome.

`ark-mcp --connect <pipe> --core-tools` lists core tools first and adds a
topic's specialist tools after `agent_onboard` loads it. It announces each change
with `notifications/tools/list_changed`, and every catalogue tool remains
callable. Without the flag, the full catalogue is listed as before.
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
