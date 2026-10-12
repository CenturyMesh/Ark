# Connect ARK, models and external agents

Two independent connections are available. Choose the one matching your workflow.

| Goal | Connection | Credentials |
|---|---|---|
| Run Single/Team inside ARK Studio | Assistant connector | ARK's ChatGPT sign-in, provider API environment variable, or local model endpoint |
| Let Codex or another MCP client edit an open Studio drawing | Live MCP | The external client manages its own model account; ARK exports the local CAD connection |
| Automate CAD without Studio | Standalone `ark-mcp` over stdio | No model account is needed by the CAD server itself |

## Codex and the open Windows drawing

1. Open the intended drawing in Studio and open **Live MCP** in Tools.
2. Select Codex and copy the generated configuration. It contains the actual
   executable path and the endpoint for this Studio window.
3. Merge the snippet into your Codex MCP configuration, preserving other servers.
   ARK does not overwrite your client settings. Reconnect the client.
4. Ask the client to call `agent_onboard`, then `doc_info`. Confirm the intended
   document and revision before authoring. `initialize` alone only checks the proxy.
5. Author with `expected_revision`. Save explicitly when the work should persist.

Example only; copy your actual values from Studio:

```toml
[mcp_servers.ark]
command = 'C:\path\to\ark-mcp.exe'
args = ['--connect', 'ark-studio-REPLACE-WITH-ACTIVE-ENDPOINT']
```

Restarting Studio changes its endpoint. Refresh the snippet and reconnect.
`--connect` never silently falls back to a different or empty document. Without
`--connect`, the process owns an independent document; its edits do not appear in
an open Studio window. Linux currently provides the standalone headless route,
not this Windows named-pipe Studio bridge.

Codex login and ARK login are separate. Connecting MCP does not transfer your
Codex credentials into ARK or authenticate the built-in assistant. The same
principle applies to Claude Code/Desktop and OpenCode configuration exports.
See [the bridge contract](CODEX_CONNECT.md), [provider options](AI_CONNECTORS.md)
and [official Codex MCP setup](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

## ChatGPT account inside ARK

Select the ChatGPT-plan connector and use **Continue with ChatGPT**. Complete the
account selection and requested consent in OpenAI's browser page. A connection
is complete only after ARK receives and validates the callback. Identity sign-in
and permission to use the plan are distinct: use **Enable plan usage** if that
permission was not granted. Select a model from the account's refreshed catalog.

ARK implements the open-source Sign in with ChatGPT flow directly against the
Responses API. It does not run Codex app-server, borrow a Codex client identity,
read `~/.codex/auth.json`, or need your Codex installation. Model listings do not
guarantee that a particular request will be eligible or fit current limits.

The Windows implementation uses PKCE, state and nonce; validates the signed
identity token; and stores its own renewable credentials with current-user DPAPI
at `%LOCALAPPDATA%\ARK-Point\chatgpt-accounts.dpapi`. Do not copy this file into
bug reports or releases. External MCP snippets contain no such credentials.
See [assistant implementation and recovery](ASSISTANT.md).

This is consistent with the documented [open-source sign-in route](https://developers.openai.com/siwc/token-sharing-open-source).
OpenAI also documents a separate [Codex app-server route](https://developers.openai.com/siwc/token-sharing-open-source/codex-app-server);
that is not ARK's current internal transport. Account eligibility and service
terms remain OpenAI's decision, not a benefit granted by ARK's Apache license.
Protocol tests do not replace a successful live sign-in and inference receipt.

## Troubleshooting

| Symptom | Check |
|---|---|
| MCP connected but nothing changes in Studio | Check `--connect`, active pipe and `doc_info`; standalone MCP owns a separate document |
| Connection stopped after restarting Studio | Copy the new endpoint and reconnect the external client |
| Account shown in browser but ARK still disconnected | Complete a fresh callback attempt; cancel expired sign-in and retry from ARK |
| Model/effort rejected | Refresh the current provider catalog and select a supported model/effort |
| Revision conflict | Inspect the current document; do not blindly replay a mutation |
| Oversized request | Inspect retained task/history; do not repeatedly resume an unchanged rejected payload |

Connecting a client grants useful CAD authority. Review the [security boundaries](../SECURITY.md)
and use synthetic documents for initial integration checks.
