# Assistant connector

ARK's native assistant connects to a Responses-compatible API. It sends the
operator's prompt and ARK tool schemas, then runs requested tools through the
host's callback. The backend has no document, filesystem, shell or credential
tool of its own. The Studio host executes document calls on its UI thread so
human edits and assistant edits share one serialized `AgentSession`.

## Configuration

`AssistantConfig` contains:

| Setting | Default / meaning |
| --- | --- |
| `endpoint` | `https://api.openai.com/v1/responses` |
| `model` | Empty; explicitly choose a model supported by the configured provider |
| `apiKeyEnv` | `OPENAI_API_KEY`, an environment-variable name, never the key itself |
| `maxRounds` | 12 HTTP responses per run, configurable from 1 to 32 |
| `timeoutSeconds` | 90 seconds total per HTTP request, configurable from 1 to 300 |
| `maxResponseBytes` | 4 MiB, also limits an individual tool result |
| `maxConversationBytes` | 8 MiB for the complete serialized request |

Set the provider key outside ARK, in the environment inherited by Studio, then
restart Studio. Do not paste credentials into chat, the model field, the endpoint
URL, a document, or a settings file. `assistantKeyAvailable()` checks only whether
the configured variable is populated; it does not verify account access or quota.

The configured endpoint is the destination for prompts, returned tool data and
the authorization header. Use the endpoint and variable belonging to the same
provider. Remote endpoints require HTTPS; embedded URL credentials, queries,
fragments and redirects are rejected. An unauthenticated local server can use
`http://localhost:PORT/v1/responses`, `127.0.0.1`, or `[::1]`, with `apiKeyEnv` empty.
No API credential is transmitted over HTTP, including loopback HTTP.

The native Windows transport uses WinHTTP with normal TLS validation, redirects,
cookies and automatic authentication disabled. It reads a key only when sending
a configured request and places it only in the HTTPS Authorization header. The
key is not included in the model input or tool callbacks. Provider HTTP error
bodies are not surfaced because arbitrary endpoints may echo sensitive headers.
Other platforms can run the offline protocol tests or provide their own transport;
the default HTTP transport currently reports an unsupported-platform error.

## Modeling loop and session behavior

The assistant is instructed to call `agent_onboard`, inspect the current revision,
and submit atomic `doc_apply` batches with `expected_revision`. Only names in the
passed MCP schema array can run. The full response is checked for unsupported
tools and repeated call IDs before its first operation is dispatched. The host's
existing tool validation remains authoritative for operation arguments.

Tool schemas use explicit `strict:false`: ARK has optional fields and extensible
geometry maps which cannot be converted to strict JSON Schema by marking every
field required. `parallel_tool_calls:false` requests sequential calls. The host
also executes returned calls in sequence.

Requests use `store:false` and retain the complete Responses output and tool
results as in-memory conversation history. Reasoning output uses
`reasoning.encrypted_content` so stateless follow-up requests can retain the
reasoning items. Text, refusals, tool call IDs and actual tool results are carried
forward. A provider implementing only Chat Completions is not compatible with
this connector. A Responses-compatible provider must support this request shape;
ARK does not silently downgrade it or select a different model.

`runAssistant()` is synchronous and belongs on a worker. It emits status, text,
tool-start and tool-end events; the HTTP response is buffered, not token-streamed.
The host is responsible for UI event marshalling and for serializing calls to its
document session. Cancellation aborts a pending native HTTP request via its
WinHTTP handle; a tool already running obeys that tool's own runtime limits.

Successful operations remain committed if a later request fails, is cancelled,
or reaches the round limit. Cancellation is not a document rollback. Use normal
Undo or inspect the document before continuing. If a run throws, the host should
start a fresh assistant history with current document inspection; no automatic
retry can duplicate an earlier mutation. Conversation size exhaustion requires a
new history rather than silently dropping tool or reasoning items.

The connector does not independently certify geometry or manufacturing output.
Its evidence is the tool result. Local save/export tools retain the filesystem
permissions and semantics documented in [the agent protocol](AGENTS.md).

## Codex

This HTTP connector is not a Codex login client or an App Server implementation.
Codex can use ARK's MCP tools through `ark-mcp`; the Studio live bridge and its
configuration are documented separately. Do not assume a standalone MCP process
shares Studio's document unless it is explicitly connected to the live bridge.

## Validation

`ark-assistant-tests` uses an injected transport and makes no external requests.
It checks schema conversion, reasoning and tool-result continuation, name
allowlisting, duplicate call IDs, incomplete responses, malformed arguments,
callback errors, cancellation, byte limits and round exhaustion. Native WinHTTP
was separately exercised against a local HTTP fixture for success, HTTP 302/401,
invalid JSON, deadline expiry, and cancellation. No paid API call was made as
part of implementation validation, so provider/model account access remains
unverified until the operator sends a configured request.

Protocol references:
[Responses function calling](https://developers.openai.com/api/docs/guides/function-calling)
and [stateless reasoning](https://developers.openai.com/api/docs/guides/reasoning).
