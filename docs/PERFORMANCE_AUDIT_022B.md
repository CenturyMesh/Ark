# 022B performance and agent contract audit

Scope: A.01, B.01/B.02/B.03 and D.02/D.04, **Parcial**. This is a source audit
and synthetic local measurement, not production qualification or a claim of
general performance improvement. Exact B-reps, semantic references, observed
revisions and atomic publication remain the acceptance authority.

## Ranked findings from the initial source audit

| Priority | Path and observed mechanism | Action and acceptance evidence |
|---|---|---|
| P1 | `src/view/direct_authoring.inl`, `previewDirectAuthoring` / `moveDirectAuthoring`: each pointer move synchronously copies an evaluated baseline and executes a complete isolated `doc_apply`. | Evaluate an immutable latest requested recipe off the UI thread; retain one running request and one replaceable pending request. Publish only matching epoch, revision and recipe generation. Validate exact preview, cancellation, failure and stale completion before accepting. Viewport integration is owned by the authoring change. |
| P1 | Same file, `beginSketchExtrusion`: selecting the original sketch starts another extrusion at its original plane, even when an extrusion already consumes it. | Distinguish editing the existing source-owned extrusion from explicitly creating another solid. Multiple consumers must produce an explicit choice. Test both sketch consumers and chained exact face extrusions. |
| P2 | Same file, `drawDirectAuthoring`: preview triangles and all triangle edges are expanded into fresh CPU vectors on every paint. | Retain render buffers per completed preview generation and upload only when that generation changes. Measure event-to-frame latency and allocation/upload counts independently of native evaluation. |
| P2 | `src/evaluate.cpp`, `Document::recompute`: thread allocation precedes semantic source discovery, which can downgrade apparently independent features into ordered barriers. | Changed: calculate eligible workers after final dependency classification. Regression covers missing semantic datums, explicit failures, subsequent correction and bounded independent evaluation. No new parallel operation classes are enabled. |
| P2 | `src/evaluate.cpp`: task duration was overwritten with the often tiny interval spent publishing an already completed future. | Changed: preserve worker evaluation duration. An observer regression lets workers complete before publication and checks that their duration survives. These durations overlap; do not sum them as document wall time. |
| P2 | `src/step_export.cpp`, `ResidentWorker::matches`: the calling thread owns its resident process and a different native thread allowance replaces it. `src/evaluate.cpp` selects different allowances at serial barriers. Core-pool scheduling also distributes consecutive jobs among different resident owners. | Instrument starts per equivalent task before changing worker custody. A stable bounded executor or stable allowance can avoid cold starts, but must preserve isolation, timeout, shutdown and the total CPU bound. No transport change is claimed here. |
| P2 | `src/agent_transactions.cpp`, `preparePreview`: `detail:changes` still constructs full before/after design inventories, with repeated row scans; rollback and failure baselines serialize/copy state. | Profile large synthetic inventories separately from OCCT. Index feature rows once and retain required rollback evidence; never weaken fingerprint or revision checks to avoid this cost. |
| P2 | `src/app/mcp_server.cpp`, initialization instructions: mandatory manufacturing setup for every mechanical concept conflicted with current concept-first policy and the in-app assistant. | Changed: open concepts start with editable design; declared processes and fabrication claims load manufacturing onboarding. Explicit user processes still apply immediately. Runtime discovery and onboarding are the authority for tool availability. |
| P3 | `docs/AGENTS.md` contains historical tool counts under several older releases. Current discovery and the versioned historical counts are easy to confuse. | Preserve historical evidence, label it by release, and check current `tools/list`. Counts alone do not prove UI/agent command parity. |

## CPU, GPU and concurrency boundaries

Independent audited feature branches already use bounded core workers. Named
sketch/datum/assembly consumers retain ordered barriers; completed bodies are
published in feature order. Immutable cache bodies are copied before lazy native
promotion. The worker process has a watchdog and request/result bounds. These
are useful existing safeguards, not evidence that every operation scales.

OCCT exposes per-operation Boolean parallelism and OBB acceleration; enabling
them requires equivalent-result and failure tests on representative solids.
Avoid global parallel-mode changes or larger tolerances as a performance shortcut.
See the [OCCT Boolean API](https://occt3d.com/dev/doc/refman/html/class_b_rep_algo_a_p_i___boolean_operation.html).
ARK already requests parallel tessellation when its isolated worker allowance is
greater than one; OCCT's [incremental mesher API](https://occt3d.com/dev/doc/refman/html/class_b_rep_mesh___incremental_mesh.html)
distinguishes the per-mesher option from a global factory default.

`src/compute.cpp` implements CUDA packed-position transforms with transfer/sync
cost calibration and CPU parity checks. It does not execute exact Booleans,
parametric constraints or B-rep topology on the GPU. It holds a shared compute
mutex during transforms, including the CPU path; profile concurrent transform
contention before changing ownership. GPU presentation and retained buffers are
appropriate for display; tessellation must not become production authority.

The follow-up implementation introduces `DirectPreviewWorker`, one pending
latest recipe, retained render vectors, and an opaque `PreparedTransfer`.
Transfer adoption checks revision, source fingerprint, expiration and custody;
normal `doc_apply` remains the publication and history boundary. A foreign
candidate ID alone cannot transfer geometry. Request-scoped native allowances
can now change within an idle resident process instead of restarting it.

Review identified additional integration gates: starting a new drag or
limit picker must invalidate an earlier pending apply; resize handle clicks must
use the retained exact cap rather than picking the surface behind the overlay;
and a background snapshot must detach the mutable shared activity ledger.
Ordinary `Document` copies share that ledger, and live activity updates do not
necessarily change the geometry revision. The dedicated capture helper strips
scratch activity; publication retains the current live activity and usage.
Shutdown must join the direct worker before the shared core thread pool is
destroyed; function-static initialization order alone does not guarantee this
when the pool is first used by a background evaluation.

Cancellation invalidates pending publication; an already executing native
operation still runs to completion or its watchdog. Shutdown can wait for that
evaluation. Per-document worker limits do not establish a process-wide CPU bound
when a direct preview and a live session recompute overlap. Portable one-shot
transport and concurrent whole-document evaluations need separate allowance
tests before extending Windows resident-worker claims to those cases.

## Synthetic measurement and proof limits

`tests/performance_022b_probe.ps1` creates a fresh standalone MCP process with
four exact boxes and four dependent transforms. It measures the same cold apply,
three height edits, one retained preview and its prepared commit. It writes no
project files and uses no user geometry. Set CPU, feature and native thread
counts explicitly for equivalent comparisons. The final kernel inspection adds
one native request; its counters are labelled accordingly. Initial transport
startup is included in the first call.

Baseline local 021B binary, CPU allowance 8 / feature workers 4 / native threads
1: cold apply 217.63 ms; warm applies 57.16, 32.53, 37.18 ms; preview 38.03 ms;
prepared commit 2.97 ms with zero evaluated features. There were 41 native
requests including the final inspection, nine process starts, and zero rejected
tool attempts. One initial script invocation was blocked by the host PowerShell
execution policy before launching MCP; the scoped `-ExecutionPolicy Bypass`
invocation ran successfully. These are a single synthetic sequence, not a
statistical comparison or a speedup claim. Nine resident starts with four
maximum active features demonstrate that process count and active concurrency
are different measurements.

The timing observer deliberately delays the owner for 300 ms to distinguish
evaluation from publication; this is a correctness test, not a benchmark.
Integrated test receipts are recorded separately from these timings. GUI drag
latency, GPU upload counts, private-project workloads, cross-product deployment
and manufacturing qualification remain outside this synthetic measurement.
Keep private activity evidence outside public documentation and source control.

## Integrated 022B comparison

The baseline was rerun immediately before the fresh 022B MCP/worker pair built
at 11:07 on 2026-10-14. No ARK test, CTest or compiler processes were observed
before the sequence. Both runs used the unchanged fixture with CPU 8 / feature
workers 4 / native threads 1, each in a fresh standalone process. The machine's
other background activity was not controlled. Times below are host wall-clock
milliseconds including MCP transport; this is one consecutive pair of sequences.

| Stage | 021B repeated baseline | 022B integrated |
|---|---:|---:|
| Cold apply | 207.63 | 165.09 |
| Warm edit 1 | 72.09 | 57.24 |
| Warm edit 2 | 41.81 | 41.23 |
| Warm edit 3 | 44.56 | 43.51 |
| Warm median | 44.56 | 43.51 |
| Retained preview | 44.60 | 39.86 |
| Prepared commit | 2.85 | 3.15 |
| Rejected tool attempts | 0 | 0 |
| Native requests including final inspection | 41 | 41 |
| Resident process starts | 9 | 9 |

Both cold/warm applies evaluated eight features and reached peak active feature
count four. Both prepared commits evaluated zero features and reused eight.
The comparison establishes successful equivalent synthetic operations and
retained-candidate reuse. It does not demonstrate a general speedup: the prepared
commit was slightly slower in this sample and earlier baseline variation is
larger than several paired differences. Fixed native allowance 1 does not
exercise allowance switching, so these runs cannot prove reduced process churn
from the new request-scoped allowance implementation. They also bypass the
Studio pointer-event queue and therefore do not quantify the asynchronous drag
or retained GPU-buffer improvements.

The measured 022B artifact SHA-256 values were
`3e179e6ffec20415d76e72a7d57b6025133f1f037ace5132d1698b618efd722f`
for `ark-mcp.exe` and
`96b203e5b22282c18f2f863d9aeb4b240a1269c7697f4a75c99b6cdc47dd7353`
for `ark-occt-worker.exe`.

Follow-up source review confirms explicit scratch-ledger detachment, invalidation
on new drags and limit/dimension modal entry, retained-cap resize handling, and
selection-buffer refresh independent of preview completion. `WinMain` now owns
a stack shutdown guard which joins the direct worker on return before static
thread-pool teardown; the later worker destructor safely repeats shutdown.
These are source-reviewed corrections; manual UI acceptance remains a separate
gate.

### Pattern consumer integration audit

Source review found two definition-selection defects in the new mixed-definition
chain patterns. `object_context` selected the seed feature and treated active
copy quantity as a dense slot range; this misidentified alternate parts,
accepted skipped slots and rejected a surviving high slot. `placement_plan`
resolved the occurrence transform but retained the seed definition for native
reference and centroid queries. Both now select the actual resolved placement
by node and stable copy, including its feature ID. Missing copies reject.
Context fingerprints include the actual placed definitions and their resources;
compact payload bounds and B-rep authority remain unchanged.

The same audit identified seed-only `doc_info` instance classification and
snapshot evidence identity; integration corrections are tracked in the
synthetic `pattern_consumer_tests.cpp` fixture. It uses two differently sized
native boxes, alternate chain slots and a skipped interior slot. The fixture
checks compact authoring/resources, occurrence membership, fingerprint changes,
exact face placement, explicit rejection and read-only state preservation.
The native `ark-pattern-consumer-tests` executable passed locally on
2026-10-14 against the coordinated 022B integration build. This validates the
synthetic consumer fixture; Studio pointer interaction remains a separate gate.

Reviewed mesh selection/cache, BOM, physics, native instance inputs and sketch
projection paths already use actual placement feature IDs. Projection also
rejects feature/copy ownership mismatches. This is a bounded source review,
not proof of every UI selection or assembly combination.

### Final gesture and integration evidence

The final Windows Release suite passed 108/108 tests, including 21 Studio checks.
Its deliberately blocked native-preview fixture processed 120 pointer events:
handler median 0.0085 ms, p95 0.0113 ms, with 118 requests coalesced. This measures
only synthetic handlers while native work is pending, not end-to-frame latency,
three-size project performance or a general speedup. The earlier comparison
table belongs to its recorded intermediate hashes, not the final preview.
Final binary hashes, inspected synthetic captures and the relocated native smoke
are recorded in [the verification receipt](verification-022B.json).
