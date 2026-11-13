# Retained previews — 032D, updated in 072A

Milestones D.02 and A.01: a validated preview retains authoring, evaluation
receipts, immutable evaluated bodies and their output order. It releases its
duplicate mutable body vector. Validated reference-frame and centroid caches
remain attached to the candidate so publication does not repeat producer queries
already needed for checks or assembly resolution.
When a legacy primitive or tube gains native B-rep evidence during a check or
reference query after evaluation, retention promotes that enriched body to a
new immutable owner. Its allocation is charged; the shared baseline body is not
mutated. This keeps the geometry that was actually checked through publication.
Snapshot, direct-preview transfer and cached apply materialize the retained
bodies without executing feature geometry again. The live document remains
unchanged until the existing revision and baseline checks permit publication.

The cache retains at most four candidates for five minutes from retention-ready
preparation. In 072A the former 128 MiB rejection becomes an adaptive **soft
retention target**, not a per-edit limit. Older cached proposals may be evicted;
the newest validated candidate remains available even when it alone exceeds that
target. `candidate_budget_is_hard_limit:false` and `max_edit_bytes:null` make this
explicit in the receipt. Geometry shared with the current live evaluation cache
is not an additional allocation. Other immutable bodies are charged once across
the retained candidates, including native JSON, mesh buffers, trimmed faces and
NURBS data. Authoring, recipes, read scopes, results and cache keys are charged
separately. Candidate-owned reference/centroid cache strings, frame values and
container allocations are also charged, including duplicated base entries.
Successful live mutations recheck allocation accounting: old geometry that is
no longer live-owned becomes chargeable and may evict older proposals. This does
not erase the newest receipt solely because of its size. Stale source/revision
checks still reject its publication without an explicit refresh. External
shared pointers do not exempt these bodies. Expiration, project boundaries,
explicit refresh, role ownership and DFM acceptance retain their existing rules.

`detail:"changes"` returns a compact `candidate_result` with revision,
evaluation counters, created IDs and postcondition receipts. Default/design
preview responses keep the full result. Geometry validity and required checks
are unchanged; response compaction is not a validation shortcut.

The synthetic allocation regression retains a small leaf edit while sharing
a base body with a 129 MiB opaque test payload. It checks body identity,
materialized payload preservation, cached apply without evaluation, and retained
custody with stale-publication rejection when that base is no longer live-owned.
Additional 072A tests allocate and charge a private 129 MiB acceleration-cache
payload through direct preview, transfer, async publication and DFM acceptance.
The payload is allocation stress, not a
native geometry qualification or a performance benchmark. Additional tests cover
transfer materialization, rejected stale adoption and failed-edit rollback.

This change reduces long-lived preview duplication. Preview evaluation and
temporary snapshot/transfer/commit materialization still use the current mutable
document representation; it does not promise a process-wide memory ceiling or
eliminate every temporary geometry copy. Focused and integration checks passed in
both native and portable builds; results and corrected regressions are recorded
in the [iteration evidence](iterations/032D.md).
