# AI connectors

Start with [Connecting](CONNECTING.md) to distinguish the built-in assistant
from an external MCP client. Provider eligibility remains provider-controlled;
configuration export is not automatic client installation or authentication.

ARK supports two separate routes: models inside Single/Team, and external agents
using the local MCP bridge to the open document. Existing ChatGPT settings remain
unchanged until the operator selects another connector. Each Team role can choose
its own connector or inherit the main Assistant configuration.

## Models inside ARK

| Provider | Route | Credential configuration |
|---|---|---|
| ChatGPT plan | Existing account-authorized Responses connector | Sign in through the existing account control |
| OpenAI API | Responses | Environment-variable name, normally `OPENAI_API_KEY` |
| Claude API | Anthropic Messages; Chat Completions compatibility also available | `ANTHROPIC_API_KEY` |
| Grok | xAI Responses | `XAI_API_KEY` |
| Grok Build | Native ACP CLI session with role-owned MCP tools | Official CLI account; sign in using `grok login` |
| Claude Code | Your installed Claude Code CLI over stream-json, with role-owned SDK MCP tools | Claude Code's own sign-in or your own key, as configured in the CLI |
| Ollama local | Local Chat Completions | No key; default loopback port 11434 |
| OpenCode Zen | Protocol appropriate to the selected catalog model | `OPENCODE_API_KEY` |
| Custom | Explicit endpoint and supported protocol | Explicit environment-variable name; loopback HTTP has no credential |

Choose an actual model ID from the provider's catalog. Refreshing a catalog is a
read-only request and does not generate a model response. Listing a model does not
prove that its tool calling, account eligibility or available hardware can complete
a CAD task. No model is silently substituted, downloaded or selected.

API connectors save environment-variable names in ARK settings. Grok Build saves
its executable path and optional model ID; the CLI owns its credentials. Do not paste secret
keys into model names, endpoints, documents or chat. A Claude/Grok chat subscription
is not assumed to be a credential for its API; an external agent manages its own
login. Requests stay on the configured route; the ARK ChatGPT token is never used
for another provider.

For Grok Build inside ARK, select **Grok Build** from the Assistant connector or
a Team card's **Connector** control. Set the official CLI executable path when
it is not installed at the normal `grok` location. Leave the model blank to use
the CLI's advertised selected model, or refresh the catalog and choose an actual
ID. Unsupported images and unnegotiated effort controls reject explicitly.
See [Grok Build setup and scope](GROK_BUILD_CONNECT.md).

For Claude Code inside ARK, select **Claude Code / CLI account**. ARK runs the
Claude Code you installed, unmodified, and executes its permitted ARK tools
itself; built-in file, shell, web and subagent tools stay disabled. The CLI
authenticates as you configured it and ARK reads no Claude credentials.
See [Claude Code inside ARK](CLAUDE_CODE_CONNECT_044B.md).

The host retains tool IDs, tool results, role permissions and observed revisions
across protocols. Incomplete or invalid responses do not execute partial tool
batches. Usage comes from provider receipts; absent usage remains unknown.
The required Messages `max_tokens` field limits one response, not the lifetime
turns or token budget of an ARK agent. Unsupported effort settings must be corrected
explicitly instead of being silently ignored.

## External agents and the open document

Live MCP exports configuration for Codex, Grok Build, Claude Code, Claude Desktop and OpenCode
using the executable and endpoint of the selected ARK process. The endpoint changes
after restart. Export does not overwrite global client settings or read client
credential stores. Start with `agent_onboard` and `doc_info` to verify the document;
MCP initialization alone does not prove that the live bridge is connected.

Grok Build uses its CLI-managed account and MCP tools, including when controlled
by its VS Code GUI. See [Grok Build setup and scope](GROK_BUILD_CONNECT.md).
This external route does not authenticate the Grok API connector inside ARK.

Claude Code/Desktop can use this stdio MCP configuration directly. OpenCode can
use the same ARK tools with Claude, Grok, a local Ollama model or another provider
configured in OpenCode. Ollama itself is a model server, so its external tool loop
is provided by an MCP client such as OpenCode. OpenCode local agents and the
OpenCode Zen hosted API are distinct routes.

The bridge enforces the current Windows user and observed document revisions;
it does not expose a public network listener. If the chosen ARK process is closed,
the connection fails explicitly instead of switching to another document.

## Availability and verification

Free model availability can change. Consult the current OpenCode catalog and its
terms; a connector preset does not guarantee free inference or permanent access.
Local Ollama models use the machine's resources and must support tool calling.
Their configured context must accommodate the tool schemas and guide; this
release does not change Ollama context settings or load/download models during
catalog discovery. Configure sufficient context in the local model/server.

See [004B verification](verification-004B.json) for the actual offline adapter,
live catalog, MCP and UI checks completed for this iteration. No claim of paid
inference validation follows from a catalog fetch or a mock transport.

Official protocol references consulted on 10 October 2026:

- [Claude API compatibility and limitations](https://platform.claude.com/docs/en/cli-sdks-libraries/libraries/openai-sdk).
- [Claude Code MCP configuration](https://code.claude.com/docs/en/mcp).
- [Grok function calling](https://docs.x.ai/developers/tools/function-calling).
- [Ollama OpenAI compatibility](https://docs.ollama.com/api/openai-compatibility).
- [OpenCode MCP](https://opencode.ai/docs/mcp-servers/) and [Zen endpoints/catalog](https://opencode.ai/docs/zen/).
