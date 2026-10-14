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
| Ollama local | Local Chat Completions | No key; default loopback port 11434 |
| OpenCode Zen | Protocol appropriate to the selected catalog model | `OPENCODE_API_KEY` |
| Custom | Explicit endpoint and supported protocol | Explicit environment-variable name; loopback HTTP has no credential |

Choose an actual model ID from the provider's catalog. Refreshing a catalog is a
read-only request and does not generate a model response. Listing a model does not
prove that its tool calling, account eligibility or available hardware can complete
a CAD task. No model is silently substituted, downloaded or selected.

Only environment-variable names are saved in ARK settings. Do not paste secret
keys into model names, endpoints, documents or chat. A Claude/Grok chat subscription
is not assumed to be a credential for its API; an external agent manages its own
login. Requests stay on the configured route; the ARK ChatGPT token is never used
for another provider.

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
