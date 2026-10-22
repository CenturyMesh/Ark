# Isolated part delegation — 052D

Scope: **B.04/D.01/D.02/D.03**, partial local implementation. Focused Studio
teams isolate PartDesigner roles. The lead first creates a coherent editable
concept and an existing assembly group; a designer refines that group in a
private document. Source publication requires lead review and integration.
Single and Parallel retain their existing behavior.

## Context and authority

The specialist receives a local brief, editable/read-only resource identities,
source revision and optional acceptance checks. It does not inherit the global
user prompt, images, sibling conversations or automatic global progress feeds.
Each assignment starts a fresh private conversation; lifetime provider usage
and request-byte accounting remain cumulative. Existing per-role model/effort
settings are preserved. No cheaper model or turn/token budget is inferred.

`team_part` is the specialist's only tool. Queries execute against an actual
`AgentSession` containing the design-container dependency closure. There is no
fallback to the source executor. Ordinary private CAD transactions enforce the
ownership manifest, native validity and local revisions. Global memory, file
I/O, external integrations, source writes and peer discovery are unavailable.

Shared parameter expressions remain authored relationships. Read-only
dependencies cannot be changed. An interface exclusively owned by the editable
part still needs an explicit acceptance condition when it must remain fixed;
isolation does not infer mechanical requirements.

## Workflow

Create a group around the already sketched part, then assign it:

```json
{"action":"assign","to":"designer","node":1,"expected_revision":12,
 "message":"Detail the support; preserve its shared mounting interface.",
 "checks":[{"query":{"tool":"eval_expr","args":{"expr":"MountPitch"},"path":"value"},
            "op":"eq","value":40}]}
```

Finish the lead turn after assignment. Only one private task may be outstanding
in this Focused workflow. The specialist queries its private document and edits
using the **local** revision, initially zero:

```json
{"action":"query","tool":"doc_info","args":{"detail":"design"}}
{"action":"apply","expected_revision":0,
 "commands":[{"op":"feature.edit","id":7,"p":{"depth":9}}]}
{"action":"submit","expected_revision":1,
 "message":"Local detail completed; shared expressions retained. Review required."}
```

Supported query tools are advertised in the schema. `preview` uses private
`doc_preview`; `apply` also accepts its prepared candidate with the local revision.
`report` with `message` requests missing inputs from the lead without publishing.
The lead may reply via `team_send` using `kind:report` to resume the designer, or
`kind:message` to retain context for its next turn. Ordinary assignment messages
cannot establish or replace private workspaces.

Submission freezes the child. The lead reviews at the original source revision:

```json
{"action":"review","task":"part-1","expected_revision":12}
```

The host derives the merge recipe and evaluates a source preview with the
immutable assignment checks. A successful review returns commands, identity
mapping, validation and `reviewed_child_sha256`. The lead integrates that digest:

```json
{"action":"integrate","task":"part-1","expected_revision":12,
 "reviewed_child_sha256":"<digest from the successful review>"}
```

Integration rechecks the merge against the current source and publishes through
ordinary atomic `doc_apply` with the same checks. Private steps and source commits
are accounted separately. Submission alone cannot satisfy global completion.
`inspect` returns task state. Lead-only `discard` abandons unpublished work; it
cannot undo an integrated task.

## Boundaries

The [container contract](DESIGN_CONTAINERS_023B.md) remains authoritative:
global Skeleton sources, cross-boundary mates and unsupported retained-feature
rewrites reject. Both source revision and authored provenance must match. Even
an unrelated source edit makes integration stale; explicitly discard/reassign or
handle the correction with the lead. There is no automatic rebase or concurrent
checkout merge extension.

Private roles omit inherited preloaded guides and automatic project-memory
prefetch; they request the relevant CAD guide inside their own workspace.
Private workspaces are session-local, not crash-persistent task queues. They do
not automatically create files or copy drawing context into project memory.
Cancellation reaches isolated native workers through native request control;
noninterruptible ordinary CPU evaluation may finish privately. Existing resource,
mailbox and native-operation guards remain; no new agent turn/token budgets.

## Comparison

The benchmark adds `PartFocused` and explicit `part_container` selectors. The
synthetic [PD01 example](../benchmarks/part_agents_052d.json) details a parametric
definition reused twice while preserving another module.

```powershell
out/052d-native/ark-benchmark.exe --suite benchmarks/part_agents_052d.json --preflight
out/052d-native/ark-benchmark.exe --suite benchmarks/part_agents_052d.json --config benchmarks/part_agents_offline_052d.json --out out/052d-part-offline.jsonl
```

Default offline PartFocused actually assigns, edits privately, submits, reviews
and integrates. Forced-delegation acceptance requires a measured private edit
and integration.
Reference commands never enter live model prompts. All strategies use the same
authored baseline and design oracle, including save/reopen and fresh authoring
reconstruction. PartFocused additionally instructs the lead to exercise the
architecture being measured; lead-only authoring cannot count as delegation.

For a neutral decision trial, set `require_part_delegation: false`, as in
[`part_agents_neutral_sol_052d.json`](../benchmarks/part_agents_neutral_sol_052d.json).
Single and PartFocused then receive the identical task text and oracle; receipts
record `user_task_sha256` to verify this. The lead may solve a small task directly.
`delegation_integrated` still distinguishes actual specialist work from a
lead-only result. Compare total team usage and latency against Single, including
all unsuccessful attempts and tool calls. Keep forced and neutral experiments
separate: they answer different questions. The offline neutral transport is
still scripted and does not demonstrate a model's delegation judgment.

Live runs require explicit `--live`. Compare the same model first. The
[`part_agents_sol_052d.json`](../benchmarks/part_agents_sol_052d.json) preset uses
`gpt-6.1-sol`, `medium`, and the already connected ARK ChatGPT account via
`auth_mode: "chatgpt-plan"`. It never imports another application's credentials.
Check the connected account's model catalog before running; availability is not
inferred from a preset. Default authentication remains `api-key` when omitted.
The benchmark host's time window bounds an experiment, not production agent turns.

```powershell
out/052d-native/ark-benchmark.exe --suite benchmarks/part_agents_052d.json --config benchmarks/part_agents_sol_052d.json --out out/052d-live-sol-medium.jsonl --live
```

Optional
`specialist_assistant` uses the explicit provider/model/protocol/endpoint fields
of `assistant` and changes only the PartFocused designer. Receipts include role
model, effort, request bytes, provider usage, private steps and integration state.
A single lead tariff cannot price a mixed-model run: aggregate monetary cost
stays unknown. Missing usage and failed attempts remain visible. Offline success
is not evidence of model quality or savings.

Observed verification and live pilot results: [052D](iterations/052D.md).
