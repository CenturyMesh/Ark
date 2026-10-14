# Local agent models (026D)

Milestones A.01/A.03, D.01/D.02. Local connector support reuses the existing ARK
agent loop, tool validation, expected-revision checks and single/team steering.

## Connect

1. Start Ollama with an installed model (`ollama list` shows the installed IDs).
2. In ARK, open **Models / connect** or **ChatGPT account**, then
   **Local models / Ollama...**. This entry is also in command search / All tools.
3. Choose the actual installed model. **Thinking effort: auto** uses its default;
   **none** asks the model to disable thinking where supported. Apply inspects
   its declared tool, completion, vision and thinking capabilities.
4. Send a task to the Assistant. Selecting a model performs no inference and
   downloads nothing. The existing ChatGPT sign-in is retained.

Windows preview:

```powershell
& 'C:\Sw_templates\Ark-024B\out\test-preview\026D-test1\ark-studio.exe'
```

For a team member, use its **Connector** button, select **ollama**, then choose
the installed model. Its **Settings** button reopens local model selection.
Other explicit card connectors are preserved. Switching the global provider
clears model/effort overrides on inherited cards so cloud model IDs cannot leak
into an Ollama request. Focused remains the default; Parallel remains optional.

The default endpoint is `http://127.0.0.1:11434/v1/chat/completions`, without an
API key. The local discovery path accepts only credential-free loopback routes.
Cloud aliases reported by Ollama are rejected in the local selector. Standard
advanced connectors remain available for other explicitly configured providers.

## What is checked

The picker reads installed IDs through `/v1/models` and the selected model's
metadata through `/api/show`. Models without declared tools/completion reject;
missing vision disables image inputs explicitly. Declaration is not proof of
successful CAD work. Existing revision and geometry validation still decides
whether a proposed tool call can be applied.

The local response timeout is 300 seconds to accommodate model loading. It is
an HTTP timeout, not a run/turn/token budget. Cancel and steering use the existing
agent controls. The provider may finish its current inference before the next
steering input is consumed. Local inference does not fall back to a cloud model.

Context size remains an Ollama setting. A model's advertised maximum context is
not necessarily the context allocated by the server. ARK does not rewrite model
files, pull weights, or change Ollama's server configuration. Complex geometry,
long histories and many tools can exceed the capability of a small local model.

## Evidence and boundaries

The deterministic connector and UI tests cover catalog routing, capability
checks, image gating, cancellation of selection, isolated card configuration and
settings persistence. The opt-in `ark-provider-connector-tests --local-agent-smoke
<model>` runs actual native box authoring in an isolated temporary session through
the ordinary assistant loop and full tool catalog. It does not touch an open user
project. Completed results and failed attempts belong in the iteration receipt.

`--local-agent-basic-smoke <model>` repeats the scratch task with only
`agent_onboard`, `doc_info` and `doc_apply`, to distinguish protocol/tool execution
from full-catalog task completion. Both smoke harnesses stop after eight responses;
this test-only bound does not change runtime agents, which retain unlimited rounds.

The local 2B trial used `qwen3.5:2b` with thinking disabled. Full-catalog execution
did not finish within the test's eight responses. In the reduced-catalog trial,
actual onboarding and document-inspection calls succeeded, but two proposed box
writes omitted the native operation. ARK returned `EVALUATION_FAILED` / `Missing
native op`, with revision 0 and `document_unchanged:true`. The reduced trial also
reached its eight-response test bound without completing. This is evidence of a
working transport/tool loop and rejected invalid authoring, not successful CAD
autonomy. The Ollama runtime reported a 262144-token allocation and one full-trial
log showed 33293 tokens with `truncated = 0`; insufficient context space was not
observed. No inference on the larger installed models was performed.

No general quality, speed, cost or model-ranking claim follows from a primitive
smoke test. Larger models, long-running design and vision quality need their own
evidence. Manufacturing drawings still require an explicit user request.

Provider references: [Ollama tool calling](https://docs.ollama.com/capabilities/tool-calling)
and [OpenAI-compatible endpoint, thinking and context settings](https://docs.ollama.com/api/openai-compatibility).
