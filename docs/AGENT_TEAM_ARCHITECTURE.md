# Agent teams and module boundaries

## Explicit completion — 005B

`Idle` and a finished provider turn are execution states, not evidence that the
assignment or user goal is complete. `team_report_task{state,summary}` records
the role's explicit report (`completed`, `needs_input`, `in_progress`). The lead
uses `team_finish_goal{state,summary}` for the entire goal after integration and
checks. Completing rejects pending jobs/mail, active peers, unreported assignments
and unresolved tool/worker failures. Summaries are agent declarations; actual
tool receipts remain authoritative and no manufacturing certification is implied.

`team_send.kind` distinguishes `assignment`, `message` and `report`. For older
clients, an omitted kind on lead-to-specialist messages still means assignment.
New user steering and accepted mutations reopen the goal. There is no new limit
on agent turns or tokens. Single mode labels a finished response separately and
asks the user to review its result, without inventing a completed task state.

ARK keeps engineering geometry in C++ and OCCT. Agent teams are a separate C++
orchestration module (`agent_team.hpp` / `agent_team.cpp`) over the existing
assistant connector. Manufacturing process inspection is another module over
the document and its evaluated evidence. Neither module substitutes generated
text or display triangles for an exact B-rep.

## Three different kinds of parallel work

- Independent model requests run on a bounded team worker pool. Each role has
  its own model, reasoning effort, conversation and provider usage receipts.
- Document operations share one serialized session executor. Revision checks
  prevent a proposal made against an old document from overwriting new work.
  Human steering also invalidates outstanding writes until the agent consumes
  the instruction. A kernel operation already executing must finish.
- The existing evaluator can run eligible independent feature branches on CPU
  workers. OCCT thread budgets remain bounded by `ARK_CPU_THREADS`,
  `ARK_FEATURE_THREADS` and `ARK_NATIVE_THREADS`. OpenGL and calibrated CUDA
  retain their existing display/mesh workloads; provider inference is not
  silently moved to the user's GPU.

These budgets address different workloads. Adding agents does not itself
accelerate one dependent Boolean operation, and additional model calls can
increase total tokens. Use the measured receipts and assign simpler review
work to an explicitly selected smaller model when suitable. Unknown usage
remains unknown rather than becoming zero.

## Collaboration and document authority

The engineer/orchestrator authors parts, sketches, datums, dimensions, materials
and manufacturing geometry, as well as assembly/mate operations and reusable
component placement. Part designers handle specifically assigned components or
refinements; the engineer retains the overall design and can carry it forward
directly. The physics reviewer remains read-only, uses actual
physics/math/geometry evidence and records unknown loads, supports and material
assumptions. Runtime command scopes enforce these responsibilities; saved role
IDs and per-role model settings remain compatible. General-purpose hosts can
retain the earlier explicit writer/read-only mode.

`team_propose` stores bounded commands/checks, sender, recipient, reason and an
inspected `expected_revision`; `team_proposal` reads them. The receiving writer
decides whether to apply with `team_apply_step`. Proposal commands are not
executed by delivery, and only a measured successful revision receipt marks a
proposal applied. A conflicting proposal needs fresh inspection and a new
baseline, rather than automatic revision substitution.

Each construction step defaults to eight commands and up to eight created features
or placements within that command bound and the role's permissions. Coherent
sketch/feature/placement changes evaluate once in an atomic batch. Independent
sketch increments remain available when useful, but are not compulsory.

The engineer can use the public document new/undo/redo/import operations with
observed revision and task authorization. Part designers cannot place assemblies
or replace documents; the reviewer remains read-only. Role-specific schemas and
instructions match the actual operation permissions, including canvas authoring.

Successful cached previews bind their actual commands/checks, role and observed
baseline. Candidate commits validate that retained recipe and pass the original
candidate ID to the host, preserving its no-recompute path. Unknown, foreign,
stale, mixed or replayed candidates reject; successful mutations clear team
candidate receipts. This does not silently rebase a preview.

ConstructionQueued, ConstructionStart and ConstructionEnd events carry actual
role/step IDs, requested operation/target names and observed revision receipts.
Queued intentions can coexist while the shared executor performs one operation.
Bounded requested sketch commands may be rendered distinctly as draft ghosts;
they are uncommitted intent, not evaluated geometry. End events clear only the
matching role/step draft. There is no authoritative interior OCCT partial shape
or interpolated construction progress. Committed features become visible at
real transaction checkpoints.

The user can steer one role or the whole team and pause or cancel subsequent
work. Events describe actual requests, tool calls, messages, errors and receipts;
they do not expose hidden reasoning or invent completion percentages.

Failed tools keep their complete response for the author. Team events and
inboxes carry bounded diagnostic details; `team_context` retains each role's
latest `last_tool_failure` with available error code, feature, revision,
unchanged-document receipt and recovery hints. These are advisory evidence,
not a new permission gate or an automatic repair. An older last failure can
remain visible after later successful edits; use its recorded revision/tool
and current document evidence before acting on it.

## Continuous work and provider failures

Studio defaults to a focused team of engineer, designer and reviewer, with no
round or turn budget (zero in `AssistantConfig.maxRounds` and
`AgentTeamConfig.maxTurnsPerRole`). `focusedWorkflow` runs one complete role
conversation at a time, even when a host supplies a larger `maxWorkers`. Only
the engineer starts. A specific `team_send` assignment queues its recipient;
the sender finishes its turn so that the recipient can run. The prompt and
delivery receipt explain this handoff rather than encouraging polling for work
that has not started. The engineer delegates a concrete component, refinement
or evidence check when useful, instead of assigning the entire goal to everyone.

Untargeted user steering reaches the engineer in focused mode. Explicit `all`
broadcasts remain available. Parallel mode retains three to eight roles with up
to six independent part designers and the configured worker concurrency;
untargeted steering broadcasts in that mode. `orchestratedStart` can still
start only the engineer and leave specialists waiting for assignment. Hosts
retain the existing parallel configuration by default; Studio selects focused
mode explicitly. Finite limits remain available for deterministic host tests.

Specialists report part IDs, evidence and unresolved decisions using
`team_send`. In focused mode, the runtime sends a completion notification if a
specialist did not already report, and always reports a failure. Parallel mode
always sends completion, since the lead may have consumed an earlier progress
message while the specialist was still running. Automatic notifications use
the current turn's text rather than resending earlier turns. Successful read
receipts stay in their author's history and events; they are not copied into
every teammate's next request. Focused updates go to the lead, and current
state and typed rejection evidence remain discoverable through `team_context`.
The shared CAD executor remains serialized and rejects stale revisions.

A transport failure, including missing terminal SSE, becomes Interrupted.
Partial output from that stream is not accepted or executed; its usage remains
unknown. Explicit resume retains the conversation and requires successful
current `doc_info` before another mutation. Cancellation, atomic writes, byte
bounds and external provider limits remain enforced. No failed transport is
silently replayed.

Static onboarding packs come from the existing C++ topic definitions, with
live document state and numeric example revisions removed. They are cached
locally by topic set and inserted once per retained conversation. This avoids
requesting the same guide for each part without caching document authority.

Manufacturing intent distinguishes sheet fabrication, machining, purchased
parts, additive manufacturing and injection molding. Inspection identifies
declared process inputs, available geometry evidence and unresolved checks.
Passing an intent checklist is not physical production qualification: strength,
fatigue, material calibration, tooling and process-specific simulation still
require evidence appropriate to the part and its use.

## DLLs, workers and language choice

The new modules currently link into the existing `ark` static library. This
keeps document ownership and the tested C++ types in one application ABI.
The OCCT executable remains an isolated worker. Loading an arbitrary DLL into
the UI process would not provide that fault isolation or make its work parallel.

ARK already exposes a C ABI in `include/ark/api.h`. A future separately shipped
module can use a versioned C boundary, opaque handles and explicit allocation
ownership, or the existing structured worker protocol. A dynamic module needs
version negotiation, capability discovery and failure/lifetime tests before
being accepted; exporting STL or OCCT objects directly across independently
compiled plugin binaries would couple their compiler and library ABIs.

Rust or Zig can implement a bounded module behind such a boundary if a measured
workload justifies it. This change introduces neither language nor a speculative
GPU geometry backend. The current C++ owners already cover the new requirements
without additional language runtimes or duplicate geometry authority.
