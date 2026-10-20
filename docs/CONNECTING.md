# Connect ARK, models and external agents

Two independent connections are available. Choose the one matching your workflow.

| Goal | Connection | Credentials |
|---|---|---|
| Run Single/Team inside ARK Studio | Assistant connector | ARK's ChatGPT sign-in, a provider API key saved in Studio or its environment variable, or a local model endpoint |
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
principle applies to Grok Build, Claude Code/Desktop and OpenCode configuration exports.
For Grok Build and its VS Code GUI, see [the CLI/MCP setup](GROK_BUILD_CONNECT.md).
See [the bridge contract](CODEX_CONNECT.md), [provider options](AI_CONNECTORS.md)
and [official Codex MCP setup](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

## Provider API keys saved in Studio

In **Assistant connector** (or an agent card connector), choose the provider,
then paste its API key in **API key / paste to save**. The field is masked. ARK
stores the key with current-user DPAPI under
`%LOCALAPPDATA%\ARK-Point\api-keys\`, bound to that provider and its endpoint
host: changing the host requires saving the key again, and a saved key is never
sent to a different host. The key is not written to Studio settings, documents,
recovery files, activity, memory or logs, and it takes effect without restarting
ARK. **Forget saved API key** removes it. When no key is saved, the provider's
API key environment variable remains the fallback (Linux and scripted runs).

The **DeepSeek API** preset uses Chat Completions at
`https://api.deepseek.com/chat/completions` with `DEEPSEEK_API_KEY` as fallback.
Enable **Refresh model catalog** and pick the current model ID the account
offers; ARK does not choose a model for you. DeepSeek thinking mode returns
`reasoning_content`, which ARK passes back in later tool rounds as DeepSeek
requires.

## Claude: API key or your Claude Code subscription

- **Claude API inside ARK:** choose the **Claude API** preset and save an
  Anthropic API key as above. Requests to the official endpoint use prompt
  caching (a breakpoint after the stable tools and system prefix, plus automatic
  caching of the growing conversation).
- **Claude subscription (Pro/Max):** ARK does not offer claude.ai sign-in and
  never handles Claude subscription credentials; Anthropic does not permit third-party
  apps to do so ([legal and compliance](https://code.claude.com/docs/en/legal-and-compliance)).
  Use your own, unmodified Claude Code instead and connect it to the open
  drawing: in **Live MCP** select `claude-code-cli` and run the two commands it
  shows (they register `ark-mcp.exe --connect <this window>` with
  `claude mcp add --transport stdio --scope user`). Claude Code keeps its own
  login and model; ARK only exports the local CAD connection. Re-run the commands
  after restarting Studio, because the endpoint changes.
- **Claude Code inside ARK:** choose the **Claude Code / CLI account** connector
  in Assistant or a Team card. ARK starts the Claude Code you installed,
  unmodified, and executes its ARK tool calls itself. You sign in through Claude
  Code (`claude`, then `/login`) or any other method it supports; ARK neither
  sees nor stores those credentials. See
  [Claude Code inside ARK](CLAUDE_CODE_CONNECT_044B.md).

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
