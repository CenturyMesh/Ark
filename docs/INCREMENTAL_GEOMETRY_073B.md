# 073B: local sketch and mate invalidation

**A.04 / B.01 / B.02 / D.02 — Verificado no escopo local.**

The observed broad rebuild had two causes. A projected sketch or supported datum
anywhere in the document caused named consumers to include unrelated resources
in their cache keys. Named extrusion consumers also included every earlier
feature generation. Editing one sketch or one mate could therefore rebuild
independent extrusions that consumed neither.

## Geometry keys and placement-only edits

`native_solid` extrusion, `extrude_surface` and `revolve_surface` now use their
complete authored profile recipe, exact named sketch/datum closure and actual
source/limit-body generations. Region selections and all authored parameters
remain in the key, including the new offset/thickness options. Join remains an
explicit Boolean consuming the target and extrusion, so changes to either
still invalidate that result.

Pose-aware profiles also include assembly/mate authoring. In ordinary assemblies
their dependency key includes both endpoints of every mate, since a changed
anchor face can move the projected occurrence even when its own body is
unchanged. Other producer families and complex assembly machinery retain the
existing conservative fallback. Named-resource execution still stays on the
owner; narrowing invalidation does not authorize incomplete worker snapshots.

Mate add/edit/remove and assembly-drive batches can use the existing guarded
placement-only path for complete healthy local-native documents. The guard
still excludes sketches with projections, supported datums, skeletons, partial
history/loading, unknown feature operations and occurrence-dependent geometry.
The assembly solver and its acceptance checks still run. This is a placement
edit, not a metadata-only publication: prepared mate candidates retain and
publish their mate authoring.

Appearance/placement-only apply receipts now report zero feature evaluation and
zero recompute time for the actual action instead of exposing stale counters.
Prepared evaluation evidence remains separate from publication timing.

## Projected sketch completion within a recompute

The previous pass-local projection cache deliberately excluded every occurrence
projection. That repeated native projection after unrelated history rows even
when the source and solved pose were already stable.

For assemblies without active patterns or drag-chain followers, 073B augments
the closure with every mate endpoint body. A projected sketch can complete only
after all those sources have published successfully, every projected-sketch
ancestor has completed, and the projection and sketch solve succeed. A later
unrelated publication reuses that exact result for the remainder of the pass.
Each edit, undo and reopen still starts a fresh pass.

Occurrence ancestry alone is insufficient: `assemblyOccurrenceDependencies`
does not include mate peers. The endpoint extension prevents stale results when
an anchor's semantic face changes, and prevents completion before a later mate
endpoint has published. Patterned assemblies, drag chains, failed projections
and unresolved closures retain repeated conservative evaluation. No projection
algorithm, geometric tolerance, body selection, revision check or failure
acceptance rule changes.

## Evidence and limits

The benchmark uses 12 independent sketch extrusions and two mated native boxes.
Its pose-aware variants add a sketch projected from the moving occurrence and a
dependent extrusion. The 072A baseline evaluated 13 of 15 features for either a
mate edit or an unrelated local sketch edit. It made 28 native requests and 17
projection refresh attempts per pass. Ordinary mate edits already reused all
local geometry, but still entered a recompute pass and reported that fact.

The first 073B checkpoint narrowed the two affected workloads to one feature
and 16 native requests, with equal final geometry. Its 150-check native suite
passed. Those intermediate receipts remain as `*-key-checkpoint.json`. The final
projection-completion change removes the remaining repeated native requests:

| Equivalent synthetic action | Complete apply 072A → 073B | Recompute 072A → 073B | Features evaluated | Native requests |
|---|---:|---:|---:|---:|
| Mate edit, local geometry only | 2.70 → 2.16 ms | 0.29 → 0 ms | 0 → 0 | 0 → 0 |
| Mate edit with pose-projected extrusion | 124.00 → 11.03 ms | 121.23 → 8.34 ms | 13 → 1 | 28 → 2 |
| Local sketch edit in that same drawing | 123.23 → 11.06 ms | 120.33 → 8.29 ms | 13 → 1 | 28 → 2 |

The two projected cases now make three refresh attempts (two waiting for sources,
one successful projection) and reuse the result after 14 later publication/end
boundaries. Both kept 14 unaffected feature bodies. The local mate case performs
no recompute pass while still solving the changed placement.

Values are medians of five measured edits after two warmups, on the same host
with CPU/feature/native settings 4/4/1 and a 256 MiB decoded native-cache ceiling.
Commands, source dimensions, resource settings and acceptance criteria matched.
All **44 final geometry records** matched exactly between 072A and 073B, and no
measured operation failed. These are complete local MCP actions with warm native
caches, not a guaranteed latency for every drawing or uncached geometry workload.

The final stable build passed eight focused suites in
`out/release-068B/dev-tests-20261108T134600437Z.xml`, including **186 incremental
geometry checks**, **149 projection/surface recompute checks** and **118 exact
projection checks**. The final benchmark receipts, original 072A records,
intermediate checkpoint and reproduction scripts are in `out/performance-073b/`;
`comparison.json` records the matched final cases.

Tests cover exact body identity/generation retention, occurrence placement,
prepared mate publication, invalid/stale edits, exact full-rebuild comparison,
source-face and supported-plane changes, Boolean targets, exact sketch-name
matching, surface/revolved profiles, changed semantic anchor faces, later mate
endpoints and patterned-copy fallback. `rebuildScene(false)` in Studio rebuilds
the display/placement representation; its name alone is not evidence of native
feature regeneration. The benchmark uses actual apply-receipt counters, with
document counters retained separately for inspection.

This is scoped incremental behavior, not universal constant-time editing or a
global topology/solver cache. Complex occurrence projections and unaudited
producer families can still do conservative work. There is no document-format
change caused by these cache keys.
