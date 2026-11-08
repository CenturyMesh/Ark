# Incremental rebuild, assembly components and materialized bodies — 074B

**A.02/A.04/B.02/B.03/B.04/D.02 — Verificado no escopo local.** This note covers
R1, R2 and the materialized-body portion of R5 in [074B](iterations/074B.md).
It records focused local evidence, not installer acceptance, hosted CI or
manufacturing qualification. Broader roadmap milestones remain **Parcial**.

## What changes

### R1: authored dependencies for more feature families

Loft, sweep, surface fill, sheet metal, surface extrusion/revolution and audited
surface edits now use the named sketches/datums they actually consume, together
with their authored definition and actual input-body generations. Audited solid
operations include extrusion, fillet, chamfer, hole, mirror and plane split.
An earlier unrelated feature edit therefore no longer invalidates those outputs.
Sheet programs still include the full parameter environment supplied to their
producer; unused parameter removal is outside this change.

Surface-edit coverage includes face extraction, plane/wire/tool trim, mirror,
ruled surfaces, pole deformation, repair, untrim, extension, mutual trim,
boundary matching, curve networks, multi-trim and trimmed extension. Unknown
operations and unaudited occurrence dependencies retain the conservative key.
The existing simple-assembly closure includes every mate endpoint producer;
patterns, drag-chain followers and late producers do not gain unsafe reuse.
Narrower invalidation does not broaden worker-snapshot or parallel eligibility.

`doc_info` evaluation diagnostics use `ark.evaluation-diagnostics.074b.v1`.
Each row reports status, dependency scope, explicit IDs/named sources and reasons
such as definition, named context, parameters, dependency or tessellation changes.
Fallback rows report a predecessor count rather than repeating the complete
history. Component fingerprints only explain a miss: the complete cache key
remains the reuse authority. Metadata and placement-only actions explicitly reset
evaluation counts instead of showing the previous geometry pass. Diagnostics are
transient and are not persisted as authored design.

### R2: independent mate components and guarded reuse

The numerical solve partitions movers by actual mate influence. Sharing a fixed
anchor does not couple otherwise independent moving groups. Exact component
solutions and their Jacobians can be retained, keyed by current authored poses,
mate definitions, resolved local frames and target values. Every exact reuse
still checks the current residuals and limits.

Changed-target warm starts are deliberately narrow: one moving B endpoint,
fixed A endpoints, distance constraints, zero local B origins and zero stored
rotation deltas. The seed is used only when it lowers the current residual cost;
a failed warm solve retries from the cold seed. Nonlinear changed problems start
cold. Cross-parent carriers, mechanical mates, active patterns and drag-chain
followers use the existing monolithic fallback. The existing numerical mover
limit and acceptance tolerances are unchanged.

The component rank calculation retains the original global column-scale
threshold. Free coordinates, rank and remaining DOFs therefore remain observable
and tested against the old monolithic solver. `diagnostics.solver` reports solved
and reused components, warm starts, iterations and Jacobian columns. Its
`residual_calls` counter counts LM residual-vector evaluations; separate
acceptance checks are not included in that counter.

Component cache retention is optional: at most 256 entries and the lesser of
64 MiB or one sixteenth of the current adaptive payload allowance. This is a
cache eviction target, never an edit-size rejection. `ARK_ASSEMBLY_SOLVER_CACHE=0`
clears/disables retention.

### R5: reuse CPU body conversion after native authentication

After the current native request has authenticated its result or validated a
saved native result, eligible features can reuse an immutable CPU `Body`
conversion. The key includes the exact native request key, complete payload
SHA-256 and producer identity. Solid postprocessing also binds its current
feature-specific tube-end evidence. A hit shares the native payload and display
mesh through the existing copy-on-write owners.

The memo never bypasses a native request, disk checksum, restore validation or
current geometry failure. New producer contracts without a verified result
identity are ineligible. Host-modified electronics, source-sheet and curve paths
remain outside this memo. Selective load and history rollback clear retention.

This memo is process-local: reopening a document in the same runtime can reuse
the conversion; restarting the process uses the persistent native-result path
and materializes a new CPU body. The demand-only target follows the adaptive
payload allowance with at most 4096 entries. Pressure/eviction only releases cache
ownership; it does not reject valid edits or promise a process-wide memory cap.
Accounting includes payload/mesh capacity and entry overhead, with periodic and
on-use refresh. Live documents may share and retain the same buffers after cache
eviction. `kernel_info.native_body_cache` exposes counts and retained bytes.
`ARK_NATIVE_BODY_CACHE=0` or `ARK_NATIVE_RESULT_CACHE=0` disables this memo.

## Comparable measurements

All fixtures are synthetic, with no user document or session. CPU allowance was
4, native threads 1 and decoded-result cache 256 MiB. Family and assembly runs
used feature-worker ceiling 4; body-memo runs used 1. Baseline 073B and candidate
074B receipts retain executable SHA-256 values, exact settings and all attempts.
The five measured family/assembly samples exclude two warmups. The memo comparison
uses the same 074B executables with only `ARK_NATIVE_BODY_CACHE` changed: eight
measured edits after four warmups and five reopens after two warmups.

| Synthetic operation | Before, median ms | After, median ms | Observed work |
|---|---:|---:|---|
| Edit unrelated box beside loft/sweep/sheet/fillet/trim | 29.817 | 3.074 | 5 evaluated / 3 reused → 1 evaluated / 7 reused |
| Geometry recompute within that edit | 28.303 | 1.967 | Same final eight feature records |
| Edit one mate among 24 independent groups | 9.765 | 3.584 | Other component solutions retained |
| Same edit plus `assembly_info`, 24 groups | 24.148 | 6.969 | Subsequent read reuses 24 components, zero Jacobian columns |
| Edit one mate among 60 independent groups | 58.325 | 8.664 | Other component solutions retained |
| Same edit plus `assembly_info`, 60 groups | 164.637 | 17.272 | Subsequent read reuses 60 components, zero Jacobian columns |
| Sphere radius 50 ↔ 51, memo off/on | 5.742 | 5.073 | 20 → 2 conversions across entire fixture, including warmup |
| Reopen same sphere in same runtime, memo off/on | 6.457 | 5.654 | 18 memo hits; 2,082,504 accounted retained bytes |

The sphere runs make the same 21 native requests. The disabled run initially
populated the disk cache; both measured windows use warmed native results.
Conversion counts include those initial operations. The entire exact property
report is equal with the memo off/on. Family final design records, including
volumes, bounds, dependencies and manufacturing metadata, are exactly equal.

All final assembly matrices and instance metadata were compared: 400 numeric
entries at 24 groups and 976 at 60 groups. Maximum absolute matrix differences
were `4.1141e-9` and `8.6021e-9`; maximum candidate positional residual was
`8.6024e-9 mm`, angular residual zero, and rank/remaining DOFs matched. The
comparison bound was `1e-7`, tighter than the unchanged solver acceptance.
The benchmark counters describe the read after the edit; changed-component
warm-start behavior is checked independently by the unit suite.

One initial fixture setup omitted the fillet selection and was rejected
atomically. That receipt is retained as `families-setup-rejection.json`; explicit
all-edge selection was added before either measured baseline. There were no
failed measured operations. No universal latency claim follows from these small,
controlled fixtures.

Local receipts and reproducible harnesses are in `out/performance-074a/`
(a development-directory name retained from preparation, not a public iteration).
`compare_core.py` checks the preserved records and writes
`core-comparison-074b.json`, including SHA-256 for every input receipt.

The table uses the final runtime recheck after the stable central Both build
`20261108T172201924Z`. Its receipts are in `out/performance-074b-final-rerun/`;
`final-runtime-recheck-manifest.json` binds the MCP, worker and Studio executable
hashes before/after all five sequential fixtures, and confirms the original
baseline/candidate receipts remain unchanged. No compiler ran during this
measured window. `final_runtime_recheck.py` in the original harness directory
reuses the exact fixtures and comparison assertions in a new evidence directory.

The first final-run invocation stopped before starting ARK because Python 3.12
did not have the harness's `psutil` dependency. Its failed preparation receipt is
retained in `out/performance-074b-final/`. The successful repetition used the
already installed Python 3.8 / psutil 6.1.1, without installing dependencies.
This is separate from the retained initial fillet-fixture rejection above.

## Focused regression evidence

The central checkpoint `dev-tests-20261108T160653630Z.xml` passed:

| Test | Checks | Evidence |
|---|---:|---|
| `incremental-families-074b` | 83 | Unrelated retention; named sketch/guide/datum/source changes; sheet bend source; exact fresh native reference; diagnostics; invalid-input rollback |
| `assembly-groups-074b` | 1893 | Monolithic cold reference; exact reuse; one changed affine group; authored free coordinate; nonlinear contact/revolute/coincident; mixed-scale rank; contradictory/ungrounded rejection and recovery |
| `native-body-cache-074b` | 13 | Across-edit/reopen ownership; authenticated disk read before hit; copy-on-write isolation; corrupt cache revalidation; partial-load/disable release |

The same checkpoint passed 134 transaction-storage checks owned by the
transaction workstream. These focused passes do not replace the final complete
native/UI/distribution gates recorded by the iteration owner.
