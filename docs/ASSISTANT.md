# Assistant connector and ChatGPT sign-in

ARK's native assistant connects to a Responses-compatible API. It sends the
operator's prompt and ARK tool schemas, then runs requested tools through the
host's callback. The backend has no document, filesystem, shell or credential
tool of its own. The Studio host executes document calls on its UI thread so
human edits and assistant edits share one serialized `AgentSession`.

Studio offers two separate credential modes: **Continue with ChatGPT** uses
OpenAI's [open-source Sign in with ChatGPT flow](https://developers.openai.com/siwc/token-sharing-open-source/sign-in)
and, when granted, the user's eligible ChatGPT plan for Responses requests.
**API key settings (advanced)** keep the existing environment-variable route.
ARK never silently switches billing modes after an authentication or plan error.

## Configuration

`AssistantConfig` contains:

| Setting | Default / meaning |
| --- | --- |
| `authMode` | `ApiKey` by default; `ChatGptPlan` selects the separate browser OAuth route |
| `endpoint` | `https://api.openai.com/v1/responses` |
| `model` | Empty; choose a provider-supported model. ChatGPT plan mode uses the selected account's visible model catalog |
| `apiKeyEnv` | `OPENAI_API_KEY` in API key mode: an environment-variable name, never the key itself |
| `maxRounds` | 12 HTTP responses per run, configurable from 1 to 32 |
| `timeoutSeconds` | 90 seconds total per HTTP request, configurable from 1 to 300 |
| `maxResponseBytes` | 4 MiB, also limits an individual tool result |
| `maxConversationBytes` | 8 MiB for the complete serialized request |

For **API key mode**, set the provider key outside ARK, in the environment inherited by Studio, then
restart Studio. Do not paste credentials into chat, the model field, the endpoint
URL, a document, or a settings file. `assistantKeyAvailable()` checks only whether
the configured variable is populated; it does not verify account access or quota.

In API key mode, the configured endpoint is the destination for prompts, returned tool data and
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

### Continue with ChatGPT

The operator starts sign-in from Studio's Account menu or launches Studio with
`--connect-chatgpt`. ARK opens the system browser only after the window has
loaded; CAD editing remains available while a worker waits for the callback.
The operator completes account selection and consent in the browser. An offline
test or a visible browser window is not proof that sign-in completed.

ARK persists a stable opaque `ext_agent_host_id` for this installation. It
listens only on `127.0.0.1` with the exact `/auth/callback` path and a selected
local port. Each attempt generates fresh OAuth `state`, OIDC `nonce` and PKCE
S256 verifier/challenge. First registration sends
`client_id=dynamic_agent_client` and `agent_name_hint=ARK-Point`; the callback's
issued `client_id` is used for code exchange and later reauthorization. Returning
sign-in reuses that issued ID and may send the retained ID token as an account
hint. The token exchange uses the same redirect URI and resource
`https://api.openai.com/v1`.

Before selecting an account, ARK verifies the ID token's RSA-SHA256 signature
against OpenAI's OIDC discovery/JWKS and checks issuer, issued-client audience,
expiration, issue time, nonce and nonempty subject. The token response must
grant `chatgpt.tokens.use.direct` before ChatGPT-plan inference. A valid
identity without that scope remains connected with plan usage disabled; an
explicit **Enable plan usage** action asks for consent again. Accounts retain
separate issued IDs, identities and token sets. Windows DPAPI protects the
app-owned file at `%LOCALAPPDATA%\ARK-Point\chatgpt-accounts.dpapi`; ARK does not
read Codex's `auth.json` or place tokens in `.ark` documents or UI settings.
Refresh uses the issued client ID, a replacement refresh token and a named
cross-process lock so two Studio instances do not race a rotating token.
Sign-out attempts revocation, clears local tokens, and reports when remote
revocation could not be confirmed. See [accounts and sessions](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions).

For plan inference, ARK fetches the signed-in account's `/v1/models` catalog,
displays entries with `visibility: "list"`, and sends the chosen `slug` to the
official `POST https://api.openai.com/v1/responses` endpoint with that account's
OAuth bearer. It pins this endpoint, sends `store:false` and `stream:true`, and
groups local ARK functions in an `ark` namespace. It consumes SSE until a
terminal `response.completed`, `response.failed` or `response.incomplete` event;
only a completed response can dispatch tool calls. Provider-reported usage on a
failed terminal response is retained before the failure is shown. `usage_record`
is never advertised as a model-callable tool. No hosted `tool_search`, connector,
file-search or computer tool is requested on this route. See [models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference)
and [preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations).

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
tool-start and tool-end events. API key mode reads a complete JSON response;
ChatGPT plan mode consumes SSE incrementally through a terminal event and then
passes that response to the same validated tool loop. Studio does not yet show
individual text deltas as they arrive.
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

## Codex and ARK login

The Studio browser flow is ARK's own official Sign in with ChatGPT client for
eligible Responses requests. It is separate from Codex CLI authentication and
is not an App Server implementation. Codex can use ARK's MCP tools through
`ark-mcp`; the Studio live bridge and its configuration are documented
separately. Do not assume a standalone MCP process shares Studio's document
unless it is explicitly connected to the live bridge.

## Validation

`ark-assistant-tests` uses an injected transport and makes no external requests.
It checks schema conversion, reasoning and tool-result continuation, name
allowlisting, duplicate call IDs, incomplete responses, malformed arguments,
callback errors, cancellation, byte limits and round exhaustion. Native WinHTTP
was separately exercised against a local HTTP fixture for success, HTTP 302/401,
invalid JSON, deadline expiry, and cancellation.

`ark-chatgpt-auth-tests` runs without reading the real account store. It checks
the RFC 7636 PKCE vector, callback state and issued-client binding, a locally
generated RSA-signed JWT against CNG/JWKS validation, rejected signature and
claim variants, SSE completion and failure handling, namespace tool allowlisting,
and DPAPI persistence in an explicitly isolated temporary directory. The
normal no-argument test never reads the Windows account profile. An explicit
`ark-chatgpt-auth-tests --status` diagnostic reads only sanitized state and
prints `available`, `connected`, `plan_authorized`, and `account_count`, with no
email, client ID, token or browser URL. The last coordinated safe status check
reported no connected account. No paid inference or completed browser consent
was verified by these offline tests; eligibility and model access require a
real user-authorized session.

Protocol references:
[Responses function calling](https://developers.openai.com/api/docs/guides/function-calling)
and [stateless reasoning](https://developers.openai.com/api/docs/guides/reasoning).

## Token usage

The Studio assistant counter sums provider-reported `usage.input_tokens`,
`output_tokens` and `total_tokens` across all response rounds for a request.
Cached input and reasoning tokens are retained as subsets; they are not added
again to the total. The usage event contains a cumulative snapshot for the run,
and the Studio transfers it onto its owning UI thread. Received usage remains
visible when a later round fails or a returned response is incomplete.

A provider that omits or returns inconsistent usage produces an unavailable or
partial counter, never an invented zero. External MCP clients do not send their
LLM usage to ARK, so their consumption is not included. The counter describes
the in-app assistant run; it is not a billing invoice or an inferred retrospective
token count. The per-run display resets on opening/creating a document; the
drawing's separate receipt ledger persists in `.ark` version 17. External clients
can supply actual provider receipts through `usage_record`, and `usage_info`
reports their provenance and incomplete coverage. ARK never infers external
MCP-agent tokens from geometry or tool calls. Tests use injected offline
responses and do not make paid requests.

See [OpenAI token accounting](https://developers.openai.com/api/docs/guides/token-counting)
for the distinction between visible text and all generated output tokens.
