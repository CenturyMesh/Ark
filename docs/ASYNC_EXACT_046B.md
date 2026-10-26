# Exact reads without blocking Studio (046B)

Milestone **A.04** remains **Parcial**. The scope below is **Verificado no escopo
local**: a synthetic freeform fixture, unit tests and Studio self-tests on one
Windows machine. Geometric authority, `expected_revision`, atomic publication
and explicit rejections are unchanged. No tolerance is relaxed and no write
leaves the Studio UI thread.

## Problem, reproduced

Every tool request that reaches Studio (live MCP, Assistant, team) runs from
the UI queue. A read on the busy list (`assembly_info`, `query_batch`,
`native_inspect`, ...) ran on the session executor while the UI thread pumped a
captured texture: the window repainted, but orbit, zoom and selection input was
removed and dropped, and every other queued call waited. A read outside that
list, or with the UI not ready, ran on the UI thread itself. The fixture below
reproduces both: an exact clearance batch takes 4.3 s, and a second client's
`doc_info` waits 3.8 s behind it.

## Contract

**Allow-list.** Only these tools may run as background jobs, derived from their
handlers in `agent.cpp`, `agent_features.cpp`, `physics.cpp` and
`motion_graph.cpp`: `native_inspect`, `geometry_query`, `reference_inspect`,
`assembly_info` (every shape, including `drive`, `sweep`, `clearance`,
`measure`, `candidate`), `collision_inspect`, `physics_inspect`, `mesh_inspect`,
`motion_info`, `step_inspect`. `query_batch` qualifies only when every row uses
one of them. These handlers read the session document and warm only caches of
the private copy (centroids, reference frames, lazily promoted native bodies,
mesh BVH, motion program). Everything else stays on the UI queue, including
writes, `doc_preview`, `sketch_repair` and plan tools (retained candidates),
exports and `snapshot` (files, viewport), `selection_set` (UI state),
`context_graph` (session index), `bom_inspect`/`bom_review` (completion gate),
memory and activity tools, cheap reads such as `doc_info`, and inspections not
reviewed here. `exactJobRejection046B` in `src/exact_jobs_046b.cpp` is the
single list; add a tool only after the same review.

**Ordering.** The UI queue stays FIFO. When an allowed request reaches its head,
the UI thread copies the session at the current revision, after every earlier
queued request, writes included, has executed. A read issued after a write
therefore sees it. A write issued after a read runs on the UI thread while the
read continues on its copy and never changes it.

**Revision labels.** The tool's own `revision` is the observed revision. Results
add `observed_revision` and `exact_job` {`id`, `execution:"background_snapshot"`,
`observed_revision`, `queue_ms`, `run_ms`, `native_request_limit_ms`,
`job_limit_ms`, `current_revision`, `document_changed_since_read`,
`document_replaced_since_read`}. A later change does not invalidate the read; it
stays labelled. The Assistant's revision tracker takes the observed revision,
so a write by another actor during the read still rejects its next call; there
is no silent rebase.

**Owner-thread effects.** Delivery runs on the UI thread, never while a busy
write owns the document. It records live activity exactly as
`AgentSession::call` does for the declared actor (agents always, live MCP
clients on failure), sets presence, status and timing, then completes the
request. Cancelled jobs record no activity.

**Cancellation.** A job stops when its live bridge `stop_token` fires (client
deadline or Studio shutdown), an Assistant/team cancel flag is set, Studio
closes, the human presses **Cancel** on the chip, or `cancelExactJob046B(id)` is
called. A running native request is polled every 50 ms; cancelling terminates
its isolated worker job, and the next request starts a new worker. Queued jobs
complete without running. A cancelled job returns `error_code:"CANCELLED"` and
never partial evidence.

**Limits.** Inside a job, one native request may run for **120 s** instead of
30 s, and the whole job is bounded at **600 s**, the live MCP execution bound.
Both are reported in `exact_job`. Timeouts keep the "worker limit" text, so the
error code stays `RESOURCE_LIMIT`. Requests outside jobs keep 30 s.

**Indicator.** While a job runs, a chip above the viewport dock shows the tool,
elapsed time and observed revision, with **Cancel**. In short viewports it can
overlap the dock. The foreground chip owns pointer input only inside its visible
bounds: Cancel precedes its neutral background, then underlying controls.
Clicks on its background do nothing; orbit, zoom and selection remain live
elsewhere. The chip is painted after drawing sheets too, matching this input
priority.

**056C integration regression (A.04).** With the assistant dock open at 1280x800,
the existing asynchronous UI test found the visible Cancel button behind the
viewport dock's `965` and `969` input zones. Foreground hit ordering and neutral
panel handling now match drawing order. The original fixture and its assertion
remain unchanged; integrated validation reruns `studio-async-exact-ui`,
`studio-viewport-dock-ui`, `studio-drawing-viewport-ui` and `studio-inertial-ui`.

**Studio API** (`src/view/exact_jobs_046b.inl`):

```cpp
using ExactJobDone046B=std::function<void(const arkjson::Value& result,u64 observedRevision)>;
static u64 submitExactJob046B(const std::string& tool,const arkjson::Value& args,ExactJobDone046B onComplete,
                              const std::string& label={},std::function<bool()> cancelled={});
static bool cancelExactJob046B(u64 id);
```

It returns 0 without calling `onComplete` when the tool or arguments are not on
the allow-list or jobs are off; the caller then uses `runTool`. `onComplete`
runs later on the UI thread. Example:

```cpp
V args=V::makeObj();args.set("detail",V::makeStr("result"));args.set("clearance",clearance);
if(!submitExactJob046B("assembly_info",args,[](const V& result,u64 revision){showClearance(result,revision);},"Check clearance"))
    showClearance(runTool("assembly_info",args),g_session.revision);
```

`ARK_STUDIO_EXACT_JOBS=0` keeps the earlier dispatch for comparisons. Headless
self-tests keep it unless they opt in.

## Thread safety

| Shared object | Treatment |
|---|---|
| Live `g_doc`/`g_session` | Copied on the UI thread; the job never reads the live objects. 128 evaluated boxes copy in 0.26 ms; the freeform fixture in 0.03-0.07 ms. |
| Evaluated geometry | Shared immutable values: `NativePayload` (const storage, `call_once` decode, atomic sizes), `MeshSnapshot`, `shared_ptr<const Body>` cache entries. Writes publish new values. No in-place `Surface` mutation was found by source search. |
| Activity ledger | Shared by ordinary copies, so it is detached; written only on the UI thread at delivery. |
| Staged saved geometry | Detached; only live recompute consumes it. |
| Private caches | Warmed in the copy and discarded; a later UI-thread read may repeat a native request. |
| Native worker IPC | The resident worker is thread-local: each of the two job threads owns its own process and channel. Cancel and limit scopes are thread-local. |
| Native result disk cache | Existing mutex-protected cache, already shared with the evaluator pool. |
| Evaluation observer | Thread-local; jobs do not feed the busy overlay. |
| Completion queue | Mutex-protected; a wake message is harmless if a busy pump drops it. |

Two job threads can each use the native thread budget (8 by default), so a
background read and a UI-thread write do not share one CPU bound. This matches
the earlier direct-preview limit.

## Measurements

Fixture: two smooth lofted solids through five 32-anchor closed splines,
interfering; `query_batch` of two exact `assembly_info` clearance rows; native
result cache disabled; i9-14900KF, RTX 4080 SUPER, 1280×800. Two other builds
were running on the machine; load was not controlled. Medians of three runs
(the UI-thread call row is one run).

Studio self-test, the same binary with the earlier dispatch (before) or a
background job (after), through the UI queue and through Studio code:

| Path | Wall time | Largest gap between live frames | Message-loop round trip, max | Second client `doc_info` | Select / zoom / orbit during the read |
|---|---|---|---|---|---|
| Queue, before (busy renderer) | 4317 ms | 4318 ms | 15.7 ms | 3818 ms | dropped |
| Queue, after | 4427 ms | 5 ms | 3.1 ms | 0 ms | applied in 2 ms |
| Studio code, before (busy renderer) | 4359 ms | 4363 ms | 15.7 ms | 3858 ms | dropped |
| Studio code, after | 4586 ms | 6 ms | 4.4 ms | 0 ms | applied in 2 ms |
| Queue, before (UI-thread call) | 5945 ms | 5951 ms | 5951 ms | 5457 ms | queued until the end |

External live MCP probe (`tests/async_exact_046b_live_probe.ps1`): a real
Studio window with an isolated profile, two `ark-mcp --connect` clients,
`SendMessageTimeout` every 20 ms, same fixture, about 20 ms resolution:

| Binary | Wall time | Round trip max | Second client `doc_info` |
|---|---|---|---|
| e8ca022 (045A) | 4369 ms | 16.8 ms | 3843 ms |
| 046B | 4433 ms | 10.8 ms | 30.6 ms |

An earlier probe run with one warm-up gave 4466 and 4623 ms. The exact read
took 1.5-5% longer than the earlier path in these runs while the live viewport
kept rendering; contention was not isolated. This is not a speedup claim.
Cancel through the chip delivered `CANCELLED` 46 ms after the click; a native
request inside a cancelled job stopped in 12-19 ms.

## Verification

```powershell
ctest --test-dir out/native-release -C Release -R '^(async-exact-046b|studio-async-exact-ui)$' --output-on-failure
powershell -NoProfile -File tests/async_exact_046b_live_probe.ps1 -StudioPath <ark-studio.exe> -McpPath <ark-mcp.exe> -Out <probe.json>
```

The two tests cover the allow-list,
write rejection, ordering after writes, revision labels, cancellation (queued,
running, external predicate, native mid-request, human chip), the 120 s/1 s
scoped limit wording, shutdown, concurrent writes during a read and a later read
that observes them. The Studio self-test asserts a live-frame gap and
message-loop round trip under 250 ms, a second client under 500 ms, input
applied during the read and cancel under 1.5 s, and writes timings beside its
captures.

## Not established

- Full feature recompute and every other write stay on the UI thread and the
  busy renderer. Recompute runs 65-120 s on large documents; no safe small step
  was found here.
- Existing Studio dialogs (mass properties, the tool command dialog, measurement
  picks) keep their earlier dispatch; only new callers use the API.
- `export_step`, `drawing_export`, `manufacturing_inspect`, `drawing_inspect`
  and the specialised inspections are not on the allow-list.
- Deliveries wait while a busy write runs or a modal dialog is open.
- Cancellation and the job bound take effect before the call and at native
  requests. Host CPU work between them, such as a large mesh self-intersection
  check, runs to completion before the job reports `CANCELLED`; closing Studio
  waits for it.
- Private caches warmed by a job are not merged back into the live document.
- Large private documents, three-size p95 qualification and the 30 s spline
  clearance seen in real sessions were not reproduced; only the synthetic
  fixture was measured.
