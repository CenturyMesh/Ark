# 031B evaluation reuse and native measurement

031B addresses **A.04**, **D.02** and the retained modeling work in **B.03**.
The evidence here covers repository-authored synthetic fixtures. A.04 and D.02
remain **Parcial** outside this scope; reuse and profiling do not expand the
geometric domain documented in [surface/sweep contracts](SURFACES_031B.md).
There is no general speedup, design-quality, inference-speed or manufacturing
qualification claim.

## Explicit feature dependencies

The evaluator can reuse an immutable evaluated body when its own recipe,
referenced expression values and declared input generations are unchanged.
031B extends the audited dependency set to bounded solid recipes, inline
surfaces and sweeps, explicit surface edits and retained mesh operations.
An unrelated earlier edit therefore need not invalidate these features.
Actual source or expression changes still invalidate the feature and its
dependents.

Eligibility requires more than a feature-type whitelist. Recursive authoring
source discovery still vetoes named sketches, custom planes, occurrence
references and discovered feature sources missing from `inputs`. These
consumers retain their ordered publication barriers and conservative context
keys. An inline profile does not exempt a hidden named source. Projected sketch
refresh retains its existing publication order.

Audited mesh workers may consume an already-evaluated triangle body through
their explicit ancestor snapshot. Lazy analytic tessellation stays on the
owner thread. Publication remains revision-bound and atomic: a rejected batch
must retain the previous authoring state, revision and immutable cache owners.

[The evaluation regression](../tests/evaluation_performance_031b_tests.cpp)
checks unrelated-edit reuse through body ownership and generation, then changes
real input geometry and expressions and checks measured dependent results.
It also checks a rejected dependent batch, independent mesh worker branches,
support-face custody and the named-sketch sweep veto. The native variant uses
exact solids, constrained fill, an inline law sweep and derived NURBS bodies;
the portable variant exercises retained mesh chains. These are correctness
checks without elapsed-time acceptance thresholds.

## Canonical history validation memo

History validation memoizes successful canonical authoring parse and roundtrip
checks for identical complete serialized bytes, including their source-format
version. The memo is thread-local and bounded. It does not accept a new state
merely because a digest matches an earlier state, and rejected/noncanonical
states do not become successful entries.

Every journal validation still verifies blob hashes, baseline and node
identity, parent/head order, provenance, delta replay and limits, resulting
state hashes, current head and undo/redo targets. External-file custody remains
a separate live document check. [History regressions](../tests/iteration_history_tests.cpp)
compare cold and warm validation and mutate an earlier node, head authoring and
undo targets after warming; each invalid journal must still reject.

## Persistent native derived results

The Windows native backend currently contains a bounded persistent cache for
allowlisted mathematical modeling requests, including normalized PCB and cable
geometry requests. Occurrence endpoints and obstacle boxes are resolved before
the native request is keyed. Its identity includes the complete resolved
request and hashes of the worker executable and adjacent DLL bytes.
Request identity sorts object keys recursively while preserving array order
and numeric values. Reordering fields during binary reload therefore retains
the same native request identity; changed input values remain separate requests.
Import, export and other external I/O requests bypass it. A worker must also
confirm the expected local dependency loading before its result can be stored
or reused. Non-Windows persistent reuse is disabled pending equivalent custody
of the actual dependency closure.

Windows hashing uses the platform CNG SHA-256 implementation, with the portable
implementation as fallback. The digest algorithm and stored bytes are unchanged;
this does not alter cache identity or the integrity boundary. The synthetic
cache regression compares a stored payload digest against portable SHA-256.

Entries are published atomically and checked for expected key, payload digest
and required native result fields. Invalid or incomplete entries miss the cache
and require normal execution. These checks detect accidental corruption; a
cache hit does not independently rerun OCCT validity or mass-property checks,
and its payload digest is not authentication against local modification.
Review covers the producer identity, loaded dependency gate and resident-worker
identity. File handles pin measured producer bytes against replacement while
their digests are retained. Results from imported files and other external I/O
remain outside this cache. The local result cache is not a portable geometry
certificate and a warm hit is not a native algorithm timing improvement.

`ARK_NATIVE_RESULT_CACHE=0` disables this cache, and
`ARK_NATIVE_CACHE_DIRECTORY` selects an isolated cache for a comparison.
[Synthetic cache regressions](../tests/native_cache_031b_tests.cpp) cover warm
result equality, changed requests, corrupted entries, rejected requests,
disabled reuse and preservation of unrelated directory contents. The cable
case requires the cached report to retain actual native obstacle-check evidence
and an edited endpoint to miss and rebuild the route. Final review and
verification results belong in the iteration evidence.

## Geometry carried by the document

Binary `.ark` files can now carry evaluated native results in the container's
separate derived section, alongside the full authoring and history root.
Editable recipes, expression inputs, retained dependencies and history remain
available for later edits and reconstruction; the snapshot does not replace them.
The reader accepts earlier containers without this section. Explicit `.arkjson`
and `.json` exports remain authoring-only. Derived bytes have a separate bounded
capacity and do not consume the existing authoring/history logical budget.
Saved exact geometry uses OCCT's native B-rep serialization. It is not embedded
STEP/STP exchange data or a generic CAD import; editable recipes remain present.
Display tessellation is omitted from embedded native results and regenerated
by the worker during validation. This avoids duplicating display triangle
buffers in the document snapshot; the separate local cache retains full results.

On load, valid authoring is parsed before derived results are staged. Normal
recomputation still resolves recipes, expressions, dependencies and live source
custody. Only an exact matching resolved request and producer identity may use
a saved native result, and its first use passes through worker validation.
Malformed optional geometry is discarded with a receipt and reconstructed
from authoring. Staging is not completed kernel validation or a replay of the
original modeling operation. `doc_load` and `doc_info` expose the transport
receipt through `derived_geometry`; this receipt is not a count of completed
native validations.

The worker parses the exact saved B-rep, checks its validity and digest,
recomputes geometric measurements and regenerates display tessellation. It
validates the topology inventory, semantic and vertex bindings, and geometric
reference descriptors against that same shape. Legitimate absent optional
metadata remains valid; inactive descriptors may be omitted or explicitly null.
Once validated, the original topology metadata representation
is preserved: downstream native requests include it, so changing its optional
fields could cause avoidable misses despite unchanged exact geometry.

New producer topology descriptors use the result entity at the emitted index.
In particular, outward face normals follow the final face orientation, rather
than the orientation of an earlier propagated reference. Saved descriptors
that disagree with the actual result still reject; this corrects production
metadata without weakening the restore checks.

Selection completion reuses the worker's privately owned, already validated
shape rather than decoding the same B-rep again. Arbitrary serialized inputs
still enter through the normal parsing and validation path. Current generic
measurements and references are distinguished from operation-specific
diagnostics saved from an earlier evaluation; those diagnostics are retained
under `saved_previous_evaluation` with `derivation_replayed:false`.

The restore scope includes Holes Orchestrator results and extrusions whose
native request has already resolved its profile and geometric inputs. They
restore saved CAD state with freshly measured exact geometry, references and
display tessellation. Their prior recipe receipts remain under
`report.saved_previous_evaluation.diagnostics`; restoring a hole does not newly
replay its entry or containment checks. `derivation_replayed` remains false.
Named sketches and live source custody are resolved through the normal host
evaluation before a native request can match.

Other stored results need operation-specific checks that this restore path does
not claim to replay. Frame, sheet, curve, constrained fill, electronics clearance
and advanced verified continuity domains retain normal derivation. Eligibility
for local caching or embedding therefore does not guarantee eligibility for snapshot
restore. The native snapshot regressions exercise hole/extrusion saved-state
validation, producer descriptors and explicit failure cases; complete-suite
evidence is recorded in the iteration receipt.

`kernel_info` distinguishes local cache hits, document restores and document
rejections. `native_document_rejection_reasons` provides a bounded distribution
of observed rejection reasons, and `native_document_last_rejection` retains a
bounded latest reason. These process-wide diagnostics explain fallback work;
they are not a per-document acceptance certificate. Rejection falls back to
normal evaluation, which must still pass its existing geometric checks.

**Verificado no escopo:** pending saved results reside in a context owned by
the loaded document. Evaluation selects that context through a scoped
thread-local binding and propagates it explicitly to audited evaluator workers.
A mutex protects that context's pending entries without locking evaluation of
other documents. JSON, history and authoring copies omit the context; a full
runtime document copy can share it under the same exact-request matching rules.
Load candidates stage into their own context. The new regressions cover loading
another document before the owner evaluates, rejected candidates and worker
propagation. These isolation cases passed the native persistence regression;
the transport receipt alone is not evidence of isolation or completed validation.

The document's evaluated native body remains available for saving when the
corresponding local cache file has been evicted. Diagnostic evaluation with
`ARK_NATIVE_RESULT_CACHE=0` omits the persistence identity, so those results
retain authoring for a later rebuild. Unsupported operations also retain their
normal evaluation path. Older source files and documents without eligible saved
results rebuild when no matching local results exist. Changed worker or
dependency bytes invalidate matching producer identity and require normal
reconstruction. The saved section does not promise that every possible feature
can open without recomputation.

[Persistence regressions](../tests/persisted_geometry_031b_tests.cpp) cover
authoring/history isolation, explicit JSON exports, invalid authoring rejection,
malformed derived fallback, eviction before saving, fresh isolated-cache
restoration and actual changed input geometry. Portable transport checks and
native geometric checks remain separate verification scopes. No first-open
duration threshold is implied by storing evaluated geometry; B-rep validation
and geometric measurements still take work.

## Cable binding context

An occurrence-bound cable resolves against the caller's current evaluated-body
frontier. A scoped binding temporarily selects that frontier and restores the
previous pointer on success or rejection. This avoids copying the authoring
document and preceding B-reps for each cable, while retaining exact reference
memoization across cable evaluations. The resolved binding report is attached
to the current result after the native request, including on a cache hit.
Numeric legacy routes bypass occurrence resolution.

[Cable regressions](../tests/assembly_sweep_031b_tests.cpp) provide preceding
bodies while the document's normal body list is absent, check successful and
rejected frontier restoration, retained reference memoization and unchanged
authored source. Existing occurrence motion, undo and sampled native collision
checks continue to govern associative routes. Copy elimination does not expand
the [sampled sweep contract](ASSEMBLY_SWEEP_031B.md).

## Native request profiling

`ARK_NATIVE_PROFILE_DIRECTORY` opts into per-request JSON traces using schema
`ark-native-request-timing-1`. Disabled profiling creates no trace files. Each
trace records the bounded operation identity, success and elapsed time, plus
per-phase call counts, inclusive time and self time. Nested phase time is
subtracted from its parent's self time.

The worker instruments dispatch, B-rep decoding/reading/validation, geometric
integration, bounds, tessellation/packing, semantic references and hole cuts.
Trace fields omit raw requests, geometry, coordinates, B-rep bytes, caller
names, paths and raw error text. Unknown operation names become `other`.
[Profiler regressions](../tests/occt_profile_031b_tests.cpp) require identical
geometry and acceptance reports with profiling enabled or disabled, bounded
trace fields, consistent nested timing and preservation of rejected geometry.

These traces measure worker phases. They do not measure the full transaction,
host queue, history validation, rendering, physical presentation or model
inference. A persistent-cache hit does not execute the OCCT phases. Profiling
overhead must be included when comparing traced runs.

## Reuse within hole operations

The hole orchestrator retains the volume measured for each validated cut result
as the source volume of the immediately following cut. Removed-volume evidence
uses the same consecutive exact results. The source is read and measured once;
each cutter and result still receives its existing checks. Final result
validation, semantic reference propagation, containment checks and explicit
failure domains remain in place.

This removes repeated integrations within one request. It introduces no shared
mutable shape cache, approximation, tolerance change or discarded solid.
The existing [hole regressions](../tests/holes_orchestrator_tests.cpp) exercise
analytic removed volume, semantic source custody and rejection paths. A timing
benefit requires a matched before/after workload; fewer repeated integrations
alone is not an end-to-end timing result.

The native generation path also avoids re-running Boolean builders whose
two-shape constructors already execute the operation. Generated results use
their locally owned exact shape to derive the selection and vertex inventories
once, after validation and serialization. Arbitrary serialized input still
passes through B-rep parsing and validation. The regression gates cover current
digest custody, invalid reference indices, malformed B-reps and the existing
precision-sensitive volume cases. The production integration algorithms and
tolerances are unchanged.

## Interpreting measurements

Legacy JSON documents and earlier binary containers without embedded evaluated
results require geometry reconstruction when matching local results are absent.
New binary containers can carry allowlisted native results in their separate
derived section. A matching request validates its saved native OCCT B-rep before
reuse; unsupported or mismatched requests follow normal reconstruction. This
physical container extension leaves the authoring source format unchanged and
does not promise five-second full reconstruction.

Warm in-document body reuse, a warm history memo, local native result reuse,
document-carried native result validation and full native evaluation are
separate modes. A comparison must retain the same authoring fixture, requested
edits, executable/dependency identity,
thread settings, geometry acceptance and failed attempts, and state which
caches were warm or disabled. An empty isolated derived-result cache does not
establish an OS-cold machine.

The focused regressions establish dependency correctness, atomicity and trace
contracts. Final full-suite results and any matched timing measurements are
recorded separately in the [031B iteration evidence](iterations/031B.md). No private
user drawing is a fixture or public benchmark input.
