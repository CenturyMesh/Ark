# 069D: incremental projection and surface recompute

**A.04 / B.03 / D.02 — Verificado no escopo local.**

069D removes two kinds of repeated runtime work after an agent or human edits
the model. Geometry acceptance, revision checks, native tolerances and atomic
publication remain unchanged. This is runtime model evaluation, separately
from the C++ development build workflow.

## Exact projected sketches

Previously, recompute refreshed every projected sketch before history, after
every feature, and at the end. A sketch whose exact sources had already been
published was projected and solved again after unrelated features. This could
dominate an edit even when almost all feature bodies were already cached.

Each recompute now owns a temporary completion table. A projected sketch is
complete only after its projection and constraint solve succeed, every semantic
feature ancestor has a successful published result, and every projected-sketch
ancestor has completed. Subsequent unrelated history rows reuse that result.
The table is discarded after the pass: source edits, undo, redo and reopen all
start fresh.

Occurrence/mate dependencies, including indirect sketch dependencies, retain
the conservative refresh behavior. Waiting, invalid or unresolved closures
continue through the existing error/retry path. Partial source readiness never
counts as completion. The parameter environment is fixed for the pass after
parameter refresh.

`doc_info` reports `projection_builds` and `projection_reused` alongside
`features_evaluated` and `features_reused`. A build counts an attempted sketch
refresh, including attempts waiting for an earlier source. These are derived
diagnostics, not persisted authoring or evidence of manufacturing suitability.

## Surface dependency closure

Four existing `surface_edit` producers now use their explicit input generation
keys rather than every earlier feature in history:

- `deform_poles`, including its optional second glue input;
- `match_boundary`, with separate source and target inputs;
- `multi_trim`, with every selected source and the final cutter;
- `extend_trimmed`, with its source face.

The owning producers validate their native fields and consume only those input
B-reps/topology and resolved scalar expressions. The generic named-resource and
occurrence veto still applies. A changed input generation or used parameter
invalidates the result. Source and target hashes still reject stale pole/Match
recipes; the cache never silently regenerates those hashes. Other surface
operations, including `curve_network`, keep their existing scheduling policy.

## Local comparison

The same synthetic commands ran against the packaged 068B runtime and 069D.
The extracted 068B MCP and worker hashes matched the initial baseline runtime.
Each workload used two warmup edits followed by five measured edits, with
`ARK_CPU_THREADS=4`, `ARK_FEATURE_THREADS=4` and `ARK_NATIVE_THREADS=1` on the same
machine. Values below are medians. Filesystem/native caches were warm; no cold
start, viewport responsiveness or general project speedup is claimed.

| Synthetic workload | Recompute 068B | Recompute 069D | Complete apply 068B → 069D |
|---|---:|---:|---:|
| 42 features, two projected circle sketches: unrelated feature edit | 945.34 ms | 21.31 ms | 948.13 → 23.82 ms |
| Same projection chain: source radius edit | 984.15 ms | 26.76 ms | 987.11 → 29.78 ms |
| Seven features, multi-trim and extension: unrelated feature edit | 28.62 ms | 0.13 ms | 29.66 → 1.04 ms |
| Same surface fixture: cutter position expression | 29.35 ms | 22.35 ms | 30.53 → 23.28 ms |
| Same surface fixture: extension margin expression | 7.40 ms | 7.38 ms | 8.45 → 8.35 ms |

The projection fixture now performs four sketch-refresh attempts (two waiting,
two successful) and reuses the completed results 84 times. The old traversal
made all 88 attempts. Both versions evaluated only one or two actual feature
bodies, demonstrating that the removed cost was outside that feature cache.
For the unrelated surface edit, evaluated features fell from three to one; for
the cutter edit, from three to two. The extension-only edit already evaluated
only one feature and shows no meaningful measured improvement.

All 40 before/after exact geometry-property comparisons matched, including
projected solid volume, trimmed/extended area, edge length, area center and
bounds. Benchmark runs recorded no failed operations. Receipts and reproducible
synthetic harnesses are in `out/performance-069d/`: `projection_benchmark.py`,
`surface_cache_benchmark.py`, the four baseline/candidate JSON reports, and
`recompute-comparison.json`.

The native `recompute-projection-069d` test passed **148 checks**, including
analytic volumes/areas, exact B-rep equality after fresh reconstruction, shared
cache custody after unrelated edits, expression changes, late source
publication, replacement sources, occurrence motion, stale source/target
hashes, suppression, forward references, stale revisions, rollback and undo /
redo. Actual source changes still trigger native evaluation or an explicit
rejection. No user drawing or session was used in these fixtures.

No document semantic version change is required. These optimizations do not
remove expensive native work when the affected geometry really changes, and
they do not qualify all surface producers for parallel evaluation.
