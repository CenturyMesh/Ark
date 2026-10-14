# Grok Build and the open ARK document

ARK's **Live MCP** client picker includes **grok-build**. It exports a native
stdio MCP connection to the current Studio window, using the same document
bridge as Codex and Claude Code. Grok Build owns its model, login, conversation
and tool loop. This is separate from the **Grok / xAI** API connector inside
ARK's Assistant and Team, which requires `XAI_API_KEY`.

This connection addresses the integration scope of **D.02**. A valid export or
MCP tool listing does not establish successful Grok inference, CAD authoring
quality, cost savings, or completion of that milestone.

## Connect from Grok Build or its VS Code GUI

1. Install the official Grok Build CLI using the provider's
   [Windows instructions](https://docs.x.ai/build/overview). If using a GUI such
   as Pawel Huryn's extension, complete its CLI installation and sign-in steps.
   Having the extension installed alone does not mean the CLI is available.
2. Run `grok login` and finish the provider's browser sign-in, or configure your
   own API key outside ARK. ARK does not read the CLI's credential files.
3. Open the intended document in ARK. Choose **Live MCP**, select **grok-build**,
   and copy the configuration for this window.
4. Merge the snippet into `%USERPROFILE%\.grok\config.toml`, or
   `$GROK_HOME/config.toml` when you use a custom Grok home. Preserve other
   settings and servers; replace an existing `mcp_servers.ark` table rather than
   creating a second table with the same name. ARK does not modify this file.
5. Reconnect Grok Build or start a new GUI session so it reloads the MCP server.
   `grok --no-auto-update mcp doctor ark --json` checks the proxy handshake and
   tool discovery; it does not verify the open Studio document. Then ask Grok
   to call `ark__agent_onboard` followed by
   `ark__doc_info` and confirm the intended document and current revision.
6. Use `expected_revision` for authoring. Initial tests should use a synthetic
   document. Inspect the actual tool receipts and visible result before saving.

Example only; use the exact paths and endpoint exported by your Studio:

```toml
[mcp_servers.ark]
command = "C:\\path\\to\\ark-mcp.exe"
args = ["--connect", "ark-studio-REPLACE-WITH-ACTIVE-ENDPOINT"]
enabled = true
```

Grok documents MCP tools as `<server>__<tool>`. The endpoint changes when Studio
restarts; export it again and reconnect. A closed window produces an explicit
transport failure, with no fallback to an unrelated document. The bridge keeps
current-user named-pipe access, atomic document changes, stale-revision rejection
and the existing tool contracts. See [the live bridge contract](CODEX_CONNECT.md).

Grok expands `${VAR}` in MCP command values. ARK therefore rejects an executable
path containing `${` instead of exporting a path that Grok might reinterpret.
The export contains no model selection, API key or inherited ARK account token.

## Accounts, models, images and limits

The provider's [Grok Build announcement](https://x.ai/news/grok-build-cli)
documents access through eligible SuperGrok and X Premium Plus accounts. CLI
authentication and eligibility remain provider-controlled. ARK does not assume
that a web subscription authorizes the xAI API route. Current subscription usage
is subject to the provider's shared pool and any separately configured additional
usage; inspect `/usage`, the account's current
[usage limits](https://docs.x.ai/x-subscription/usage-limits) and
[pricing](https://x.ai/pricing). ARK neither enables top-ups nor guarantees free
or unlimited inference. External-client usage remains unknown in ARK unless an
actual receipt is recorded.

The GUI [maintainer's documentation](https://github.com/phuryn/grok-build-vscode)
describes vision attachments and CLI-owned model/session selection. Configure
these in Grok Build; the MCP export does not choose a model, reasoning effort or
image-generation capability. Image support must be checked against the actual
runtime: the tested unauthenticated Grok 1.0.52 ACP handshake advertised `image:false` and
`audio:false`, so the GUI feature list alone does not establish ACP image support
for that runtime. Reference images and generated assets remain
distinct from exact geometric authority. Import an actual image artifact with
the existing ARK image tools when needed; screenshots do not select B-rep geometry.

## ACP and the built-in Assistant

Grok Build also offers the official
[ACP stdio interface](https://docs.x.ai/build/cli/headless-scripting),
`grok --no-auto-update agent stdio`. The VS Code GUI uses that interface. ACP
manages agent sessions: authentication, incremental `session/update` messages,
permission requests, model selection, cancellation and MCP tools. It does not
return the Responses-style tool-request envelope expected by ARK's
`runAssistant` provider transport.

028D adds that separate ACP client to Windows Single Assistant and Team cards.
The native CLI transport is currently Windows-only. Choose
**Grok Build** in the connector control after completing `grok login`. The CLI
field accepts `grok` at the normal installation location or the official
executable's full path. A blank model uses the selected model advertised by the
CLI; an explicit model must occur in its negotiated catalog.

ARK starts an isolated CLI process with external filesystem, terminal, hooks,
subagents and inherited MCP servers disabled. It exposes only the current
role's model-visible ARK tools through reverse MCP. Role permissions, command
scope, `expected_revision`, atomic publication, steering and actual tool receipts
remain host-owned. Cancellation retains completed commits; uncertain interruption
requires a successful current-document inspection before resumed writes.

The tested CLI advertises no ACP image capability, so ARK rejects image inputs
on this route. Model discovery starts no inference. The CLI keeps authentication
and provider eligibility; this route does not supply `XAI_API_KEY` or transfer
credentials into the existing API connector. Usage without a receipt remains
unknown. See [028D evidence](iterations/028D.md) for native protocol/UI checks and
the separate authenticated-authoring gate.

Official CLI/MCP/ACP and GUI-maintainer references checked on 14 October 2026.
See [the earlier external-MCP development scope](verification-grok-build.json)
and [028D's native CLI scope and release receipts](iterations/028D.md).
Discovery alone does not establish authenticated inference or CAD quality.
