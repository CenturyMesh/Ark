# 072A: retain a validated edit without a fixed memory ceiling

**A.04 / B.01 / D.02 — Verificado no escopo local.**

The former 128 MiB limit could reject or discard an already validated preview,
its background transfer, or a manufacturing comparison. 072A removes that
per-edit quota. The candidate cache instead uses the hardware policy's snapshot
allowance as a **soft retention target**: it may release older cached proposals,
but keeps the newest validated candidate even when that candidate alone exceeds
the target. Serialized tool-boundary cleanup follows the same rule.

This is not a larger fixed edit limit. A zero or very small retention target
still permits the newest validated edit. Actual geometry, snapshot and
materialization allocation failures remain failures; no geometry is discarded,
approximated or recomputed against a different source to obtain a success.
Optional cache-policy lookup failure falls back to newest-only retention, and
memory-accounting allocation failure evicts older entries without erasing the
last candidate or turning post-commit cache maintenance into an edit failure.

The receipt exposes:

- `candidate_retention_target_bytes`: the observed adaptive soft target;
- `candidate_budget_bytes`: its compatibility alias;
- `candidate_budget_is_hard_limit:false`, `candidate_hard_limit_bytes:null` and
  `max_edit_bytes:null`;
- additional/retained accounted bytes, retained candidate count and whether
  retention exceeds the soft target.

Accounting excludes geometry shared with the live document and counts shared
immutable owners once. It remains conservative and is not a process-RAM peak.
Existing native packing preserves byte-identical evidence; it is not a reason
to drop surfaces, solids, references or checks.

Prepared transfer adoption still verifies candidate custody, baseline
fingerprint, source revision, expiry and completeness. Async publication also
verifies the document epoch and cancellation state. A DFM proposal retains its
measured comparison and still requires `accept_dfm:true`; larger memory usage
does not waive acceptance or qualify manufacture. Stale ordinary previews need
the existing explicit refresh workflow; stale DFM proposals need a new explicit
comparison.

The cache still retains at most four proposals. Its five-minute review lifetime
starts when compaction/accounting and the receipt are ready, and DFM comparison
completion starts its review lifetime. Project replacement, expiry, explicit
acceptance and role-ownership rules remain in force.

Cached publication assembles its response before replacing the live document.
Metadata-only publication moves the already validated authoring arrays instead
of allocating another copy after `remember()`. It retains evaluated body
identity and newer live usage/activity, and continues through the normal
history/provenance transaction.

## Verification fixtures

`candidate-retention-072a` allocates a real private 129 MiB acceleration-cache
field attached to an unchanged public box. Prepared documents copy and charge
that private map; this tests allocation/custody without presenting opaque bytes
as native B-rep evidence. Box geometry and the native DFM geometry still run
their normal validation.

The cases cover direct preview and no-recompute apply above the former limit;
1 MiB, zero and 128 MiB soft targets; aggregate retention of two 65 MiB candidates;
read/failure boundary cleanup; stale adoption; async transfer and publication;
replacement-epoch rejection; injected allocation-failure rollback; explicit
DFM acceptance and analytical native volume; byte-identical retained stress
data; and metadata-only body identity, newer usage, provenance and undo.

The allocation exception is injected at the async preparation hook, not caused
by exhausting the machine's physical RAM. This does not qualify arbitrary
geometry construction or promise unlimited available memory. The test uses no
user drawing and does not mutate an installed Studio session.

The focused native run passed **65 checks** in `candidate-retention-072a` and
**38 checks** in `candidate_storage-032d`. The receipt is
`out/release-068B/dev-tests-20261108T111113763Z.xml`. The wider release verification
and the separate large-history UI checks belong to the 072A iteration evidence.
