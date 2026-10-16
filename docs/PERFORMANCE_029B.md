# A.04 — native viewport reuse and bounded evaluation context

029B keeps **A.04 Parcial**. This followup addresses actual native modeling and
viewport work in addition to the earlier `doc_info` optimization. All fixtures
are repository-authored synthetic assemblies. Customer-project, physical
presentation and OS-cold qualification remain separate work.

## Runtime changes

The viewport retains CPU meshes, semantic face/edge picks, mesh audits, BVHs and
GL buffers for unchanged evaluated definitions. Occurrences retain individual
placements. Reuse requires the same immutable evaluation-cache body owner,
current native topology registry and unchanged display-buffer serial. Native
promotion, a different document, geometry/normal changes or animated display
mutations invalidate the relevant representation. Changed geometry may reuse
its GL allocation while uploading new contents; removed parts release buffers.
Selection follows stable feature/node/copy identity across part-index changes.

Audited parallel evaluation jobs copy their explicit ancestor recipes instead
of every unrelated feature. Named sketch/datum/instance consumers retain their
ordered publication barriers. History-prefix eligibility is computed in one
pass. Worker progress waits can wake when the result becomes ready instead of
always sleeping the entire progress interval. Revision guards, exact geometric
acceptance and atomic publication remain intact.

## Comparable native workload

`ark-studio --self-test-native-product-performance-ui` creates exact OCCT
plate/cylinder assemblies of 12, 48 and 144 occurrences with one editable fillet.
It runs full-rebuild and reuse modes in the same executable with identical
fixture source hashes, 3 warmups and 21 measured edits per size/mode. Each edit
uses the real parameter editor and revision-checked transaction. Assertions
verify the expected native recompute count, valid B-reps, occurrence inventory
and exactly one rebuilt part in reuse mode.

Raw reports retain every attempt, fixture/executable hashes, machine/render
settings, input expressions and separate recompute/scene/draw/SwapBuffers API
timings. Full and reuse modes share the final evaluator/kernel, so this
comparison isolates viewport reuse rather than claiming an OCCT algorithm
speedup. Automated dialog dwell is excluded. A hidden OpenGL context and
SwapBuffers return do not measure physical input-to-pixel latency.

The final local run completed all **144 attempts** (126 measured, 18 warmups)
without failure. The three fixture hashes and ordered edit expressions matched
between modes. Both modes used the same evaluated native geometry and runtime.

| Native occurrences | Full scene p95, ms | Reused scene p95, ms | Full Apply-to-scene p95, ms | Reused Apply-to-scene p95, ms |
|---|---:|---:|---:|---:|
| 12 | 1.5202 | 0.5451 | 61.3850 | 37.1720 |
| 48 | 5.1996 | 1.5721 | 69.2854 | 65.6889 |
| 144 | 14.3513 | 4.4482 | 149.1689 | 140.4780 |

At 144 occurrences, reuse rebuilt one part and retained 143 representations.
The smaller end-to-end difference includes transaction, recompute and progress
work that the viewport cache does not eliminate. Mode order and every raw sample
remain in the [final report](verification/029B/studio-native-performance-final.json).
The [earlier exploratory run](verification/029B/studio-native-performance-exploratory.json)
also passed and is retained separately; the final implementation additionally
keeps GL allocations for changed definitions. These are local observations,
not universal acceptance thresholds.

The final run used the extracted development ZIP with an isolated profile and
the SDK excluded from PATH. Its Studio SHA-256 is
`676546b3e6d353012989d96aa446eb25c8b96ada43135bec08481c5757588357`.
The [A4 checkpoint](verification/029B/studio-native-performance-a4.json) is
retained separately: that focused benchmark passed, but the full native suite
found a nearly-null tangent solver regression in another workflow. The solver
was corrected and the benchmark repeated against the final payload. A passing
focused benchmark alone did not qualify the earlier package.

The unchanged analytic performance harness separately permits matched
before/after comparisons of the parallel recompute changes. No result from
that workload alone qualifies native OCCT geometry.

The local 101-sample comparison used the same compiler flags, machine, workload
and fixture hashes. Changed-parameter recompute p95 was:

| Analytic features | Before, ms | After, ms | Reduction |
|---|---:|---:|---:|
| 16 | 0.5025 | 0.3078 | 38.75% |
| 128 | 7.0182 | 2.2469 | 67.98% |
| 512 | 72.2077 | 8.5356 | 88.18% |

These are observed results for independent analytic definitions. The archived
before runtime also predates the earlier `doc_info` fix; the table above reports
recompute, not a general speedup attributable to one line of code. Raw
[before](verification/029B/performance-recompute-before.json) and
[after](verification/029B/performance-recompute-after.json) retain the other
measured stages and runtime identities, including any regressions.

## Correctness and limits

`--self-test-scene-cache-ui` compares reused scenes to forced full rebuilds,
including native edits, undo/redo, changed part order, occurrence movement,
appearance, normals, GPU display mutations, document replacement, unloading,
buffer deletion and native promotion with semantic face picking. The
assistance/DPI checks inspect the affected native controls and captures.

Local measurements and all unsuccessful attempts are recorded in
[029B verification](VERIFICATION_029B.md). These observations do not set a
universal p95 threshold, prove proportional RSS reduction or qualify every
project, driver, monitor or manufacturing workflow.
