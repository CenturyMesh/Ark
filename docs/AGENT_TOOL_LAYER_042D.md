# Agent tool layer — 042D

042D reduces provider rounds that author no geometry and keeps request prefixes
stable. It addresses **A.04/D.01/D.02/D.04/E.01**, which stay **Parcial**. The
contracts below are **Verificado no escopo local** by offline tests. No
real-model run, cost reduction or design-quality change is claimed; the A/B
procedure at the end defines the evidence that would.

Geometric authority, `expected_revision`, atomic publication and explicit
rejections are unchanged. Nothing here adds a turn or token budget.

## Ratings inside team reports (D.02)

`team_report_task` and `team_finish_goal` accept an optional `ratings` array.
Its items are the same as `tool_feedback` ratings: `activity_id`, `utility` and
`difficulty` from 1 to 5, `action`, and optional `issue`, `wished_tool` and
`wished_options`.

- The report's own checks run first, counting those IDs as rated.
- The ratings are then recorded through the role's audited executor. It
  validates every item before applying any.
- The report is recorded only after both steps succeed.
- An invalid, duplicate or partial set rejects the report, lists the remaining
  `unrated_activity_ids` and records nothing. The field is named apart from
  `pending_feedback_ids`, so a rejected report does not make the turn-end
  feedback check demand ratings for a report that is not completing.
- The accepted receipt returns `rated_ids`. The role and the assistant loop then
  clear those IDs without another provider round.

`tool_feedback` remains the route for the single assistant, ACP and external MCP
clients. See [AGENT_ACTIVITY.md](AGENT_ACTIVITY.md) for the ledger rules.

**Held final replies.** When a final reply is held because ratings are missing,
the reply is kept. If the next round contains only `tool_feedback` or
`activity_info` calls, no new text, and leaves nothing unrated, the held reply
becomes the final reply. It passes the same BOM, pending-input and acceptance
checks, and no provider round is spent repeating it. This applies to the single
assistant and to every team role's turn end.

## Handoff notices (D.02)

When another role's turn ends, the notice delivered to the lead now carries:
- that role's explicit task report (state and summary);
- its committed steps and unresolved tool errors;
- the last document revision the team observed.

The notice no longer asks for `team_context` and `doc_info`. It asks the lead to
inspect only what it will integrate: `object_context` for the reported IDs, or
`doc_info detail:digest` with its fingerprints. `team_context` is still
available when another role matters.

## Batch dataflow (D.02, D.04)

`query_batch` rows may carry:

| Field | Meaning |
|---|---|
| `id` | 1–32 letters, digits, `_` or `-`; unique in the batch |
| `{"$ref":"id.path"}` | Anywhere in `args`: the value at `path` in an earlier row's full result |
| `paths` | 1–32 nested values to return instead of the whole result; not combined with `fields` |
| `max_bytes` | 256–262,144; a larger row returns its shape instead of its data |

**Paths.** A path joins steps with dots. Each step is one of:
- `key`;
- `key[n]`: one element;
- `key[a:b]`: each element in that range;
- `key[]`: each element.

There are no wildcards, filters, loops or code. Inside an element map an absent
value is `null`; elsewhere it is a row error that names the missing step and the
keys available there.

**Example.**

```json
{"queries":[
  {"id":"summary","tool":"doc_info","args":{},"paths":["features[0:16].id","feature_count"]},
  {"tool":"object_context","args":{"ids":{"$ref":"summary.features[0:16].id"}},"paths":["objects[].id","objects[].bounds_mm"]}
]}
```

**Execution.**
- Every structure, path and reference is validated before any row runs.
- References may name earlier rows only.
- Rows run once, in order, inside one call, so they observe one revision.
- A referenced value is read from the earlier row's full result, before that
  row's own projection. Each value is limited to 64 KiB.

**Receipts.**
- A row echoes its submitted `args`.
- When references were resolved, the row also returns `resolved_args`. Team
  failure retention uses `resolved_args`, so a recovery read never repeats an
  unresolved reference.

**Row errors.**

| Code | When |
|---|---|
| `QUERY_BATCH_ROW_SKIPPED` | The row references a row that failed |
| `QUERY_BATCH_REF_UNRESOLVED` | A reference names an absent value |
| `QUERY_BATCH_PATH_MISSING` | A requested path is absent; returns the available fields, without a full result dump |
| `QUERY_BATCH_ROW_TOO_LARGE` | The row is above `max_bytes`; returns each top-level field's type, count and bytes, and from 047D the child key names of object fields (up to 32, `keys_truncated` beyond) |

Other rows are unaffected. Batches without these fields keep the
[032D contract](AGENT_CONTRACTS_032D.md).

## Revision digest (E.01, D.04)

`doc_info {detail:"digest"}` returns `ark.design-digest.v1`:
- revision, `source_sha256` and counts;
- compact feature rows: id, name, type, ok, inputs, material, part name and
  bounds;
- sketch rows: plane, profile state, entity count and consumers;
- assembly nodes, parameters and material names;
- a `fingerprints` map.

Each fingerprint covers that object's full detail-design row. Sending the map
back as `known_fingerprints` returns rows only for changed or new objects, with
`changed`, `unchanged` and `removed` keys. On the local 12-box fixture the
digest is less than half the size of detail design.

The digest is built in-app from the authored state, is deterministic for one
revision and is never written to disk. Fingerprints detect change only. They
never replace `expected_revision`, and the digest is not validation.

## Request options for A/B runs (A.04)

These options are off by default. Studio reads them when a run starts:

| Variable | Option | Bounds |
|---|---|---|
| `ARK_RESULT_HANDLE_BYTES` | `AssistantConfig::resultHandleBytes` | 4096–1,048,576 |
| `ARK_PROJECTION_STEP_BYTES` | `AssistantConfig::projectionStepBytes` | 16,384–4,194,304 |
| `ARK_PROMPT_CACHE_KEY=role` | `AssistantConfig::stablePromptCacheKey` | API-key OpenAI only |
| `ARK_DEFER_SPECIALIST_TOOLS=0` | `AssistantConfig::deferSpecialistTools=false` | — |
| `ARK_ANTHROPIC_TOOL_ADDITIONS=1` | `AssistantConfig::anthropicToolAdditions` | first-party Claude Messages only |

**Deferred tools on Claude.** `ARK_ANTHROPIC_TOOL_ADDITIONS=1` applies to the
first-party Messages endpoint, on models that accept mid-conversation tool
changes: Claude Opus 5 and 5.5, Opus 4.8, Fable 5 and 5.1, Mythos 5 and Sonnet 5.5.
- Every request declares the whole tool list, with specialist tools marked
  `defer_loading`. The request then carries the
  `mid-conversation-tool-changes-2026-07-01` beta header.
- When a topic loads, a `role:"system"` message with `tool_addition` blocks for
  its tools follows the tool result that loaded it.
- The declared tools and the earlier history stay byte-identical.
- If history projection has removed that tool result, those tools are sent
  without `defer_loading`, so they stay callable.
- The request shape is tested offline only; the beta has not been exercised
  against the live API.

**Fixed tool list.** `ARK_DEFER_SPECIALIST_TOOLS=0` advertises every tool of
the role from the first request. Loading a topic then never changes the tool
list, at the cost of more schema bytes in every round.

**Stable cache key.** `ARK_PROMPT_CACHE_KEY=role` keys OpenAI prompt caching by
model, instructions, role and leading guide, without the tool list or the
projection header. The ChatGPT plan path stays unkeyed.

**Result handles.** A successful read whose structured result is larger than
`resultHandleBytes` enters history as a handle receipt. The receipt contains:
- the result's scalar fields;
- activity, rating and team-progress fields;
- the shape of every other field;
- the handle.

`result_get {handle, paths, max_bytes?}` returns selected values from the held
result without calling the tool again.
- Held results stay in memory for the turn that produced them, up to 64 MiB.
- An unknown or expired handle is rejected as `RESULT_HANDLE_EXPIRED`. The
  receipt still names the original tool and arguments, so the read can be repeated.
- Writes, rejections, guides, snapshots, previews, feedback and team
  coordination are never held.

**Stable projection.** Once history exceeds the proactive projection threshold,
the projected prefix is kept byte-identical. New items are appended verbatim
until the request grows by `projectionStepBytes`; then the history is projected
again.
- This path skips graph deduplication, because it rewrites earlier items.
- It costs more request bytes per round than projecting every round. It helps
  only when the provider actually caches the prefix, so it must be measured per
  provider.

## Cache-stable Messages notes (A.04)

On the first-party Messages endpoint, these models keep a host note in place:
Claude Opus 5 and 5.5, Opus 4.8, Fable 5 and 5.1, Mythos 5 and Sonnet 5.5, and
Haiku 5.5. A host note is a reminder, repeat notice, steering message or
projection header added after the conversation starts.
- After a tool result, the note becomes a `role:"system"` message between the
  user and assistant turns.
- After an assistant reply, it becomes a user text block labelled
  `ARK host note:`.

Leading instructions stay in the top-level system prompt. Other models keep
the earlier behaviour, which adds the note to the system prompt.

## Trace attribution (D.01)

`ARK_ASSISTANT_TRACE` `provider_round` rows now record:
- `provider`, `model` and `role` (the team role id);
- `round_kind` (`write`, `read`, `feedback`, `coordination`, `text` or
  `mixed`) and the called tool names;
- `tools_bytes`, `instruction_bytes`, `tool_result_bytes` and `message_bytes`;
- `input_composition_bytes`, split by reasoning, tool calls, tool results,
  developer, user and assistant items;
- `prompt_cache_key` when one is sent.

Tool rows add `role` and `history_bytes` beside the full `result_bytes`. Rows
from concurrent roles are written serially to the same file.

## Live MCP progress (A.04)

`ark-mcp --connect` forwards Studio progress as MCP `notifications/progress`
when a `tools/call` carries `_meta.progressToken`.
- Studio sends a note about every two seconds while it waits for the result:
  elapsed time and, during a recompute, the feature being evaluated.
- A client that does not ask receives only the result, as before. Studio
  builds without this support ignore the request.
- Progress never changes the outcome. Calls stay serial on the document
  thread, with the same limits.

**Several pipe instances.** Studio now serves the live pipe through four
instances.
- A further client connects and queues while one call runs, instead of waiting
  out the 30-second connect window behind a long recompute.
- The first instance still claims the name exclusively.
- The document thread still executes one call at a time.

**Core tools for external clients.** `ark-mcp --connect <pipe> --core-tools`
lists core tools plus the topics loaded through `agent_onboard`, as the in-app
agents see them.
- It declares `tools.listChanged` and sends `notifications/tools/list_changed`
  after an onboarding call that changes the list.
- Any catalogue tool is still executed when called.

Concurrent reads against a revision snapshot are not implemented. The
snapshot cost has to be measured on a representative model first.

## Small fixes

- An empty `query` beside `ids` or `keys` is treated as absent in
  `object_context` and `context_graph`.
- The assistant instructions ask writers to put verification in a write's
  `checks` instead of a separate read round, and point to `doc_info
  detail:digest` for orientation in an existing design.
- [CODEX_CONNECT.md](CODEX_CONNECT.md) now states the 30-second
  connect/transfer windows and the 10-minute result wait.

## A/B procedure

**Arms.** The baseline, then one change per arm:
- ratings in reports;
- batch dataflow;
- digest;
- handoff notices;
- result handles;
- stable projection;
- fixed tool list;
- stable cache key, which needs the OpenAI API-key path;
- Messages notes, which needs a Claude model.

**Controls.** Keep everything else equal: briefs, model, reasoning effort, mode
(Team Focused unless stated), onboarding packs and machine. Run at least three
repeats per arm and brief, interleaved.

**Record per run** from `provider_round` rows and `studio_team_diagnostics`:
- rounds, calls, input, cached, uncached, output and reasoning tokens, split by
  `round_kind`;
- rejected writes and repeated reads;
- rating coverage at completion;
- features, assembly nodes, reviewer findings resolved and goal state;
- wall time.

**Primary metric.** Uncached input summed over all runs of an arm, including
failed and interrupted runs, divided by the number of accepted designs. Fix the
acceptance criteria before running.

**Scope of results.** These are local proof on one machine, not deployment,
production or fabrication evidence.
