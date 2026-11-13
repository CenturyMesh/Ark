# Growing documents and retained edits — 072A

**A.02 / A.03 / A.04 / B.01 / B.02 / D.02 — Parcial.** Implementation is prepared;
centralized native, persistence and affected UI verification is pending. This
document describes the contracts, not completed distribution qualification.

## Editing and retained candidates

The previous 128 MiB additional-allocation threshold no longer rejects an edit
or prevents retaining its newest validated candidate. Retention uses the current
hardware/available-memory policy as a **soft target**: older cached proposals can
be evicted, while the newest successfully prepared proposal remains available
even above that target. Existing candidate expiry, ownership, source hashes and
revision checks still apply. Allocation failures remain explicit failures.

Receipts distinguish `candidate_retention_target_bytes` from a hard limit,
report accounted retained/additional allocations and identify when the soft
target was exceeded. Shared live geometry is not charged again as a separate
retained allocation. These measurements do not describe total process memory.
Evicting an uncommitted cached proposal never deletes authored history.

## History and authoring

The fixed 4 MiB logical-document ceiling, 512-node journal ceiling and fixed
replay-operation ceilings are removed. Successful edits retain their earlier
nodes, branches, source blobs and identities. History is checked through its
actual indexed graph, including missing parents, cycles, source hashes, deltas
and undo/redo references. No automatic summarization, pruning or lossy rewrite
is introduced.

`history_storage` and `history_info` report actual node counts and serialized
source/history bytes. They do not present a percentage of a discontinued quota.
The compatibility fields `max_nodes` and `max_document_bytes` are null, with
`growth_policy: resource_limited_no_automatic_pruning`. The design can still
fail validation or run out of memory or storage; null does not mean infinite
physical resources.

Explicit archive-backed history compaction remains a separate human/agent action
requiring its existing confirmation and a verified complete archive. It is not
performed by normal editing, saving or upgrading the runtime.

## Physical storage compatibility

The authored `.ark` semantic format remains at its existing maximum **57**.
Capacity changes do not change model semantics or regenerate history node IDs.
The binary value codec and canonical payload/root hashes remain unchanged.

Physical SQLite container **v3** removes the old `ark_meta.logical_bytes` 4 MiB
constraint. Small compatible writes keep v1/v2. A write switches to v3 when its
source, encoded data or retained history exceeds an older reader's capacity:
4 MiB logical source, 16 MiB encoded/root or aggregate payload data, 8192 slots,
the old value-item count, 512 history nodes or the older replay-operation bounds.
These are compatibility signals, not new rejection thresholds.
Replay compatibility includes re-reading an ancestor chain on each branch switch,
not just summing the deltas stored in nodes.

The metadata-table upgrade runs inside the same SQLite transaction as the save.
Existing payloads and slots remain content-addressed; source bytes and history
are not compacted away. v3 retains the optional derived table, which can be empty.
Later smaller writes retain v3. Readers still accept original v1/v2 containers.
**A v3 file requires ARK 072A or later**; an older reader rejects that physical
format explicitly. Keep earlier archives when an older runtime is needed.

SQLite uses its backend page/row/integer limits rather than the previous
application-level 64/256 MiB file ceilings. Loading reads the required SQLite
rows, not one allocation containing the complete physical file. Disk-full,
locking, interrupted writes, schema mismatch and checksum failures remain atomic
rejections that preserve the preceding valid document.

## Protecting reads with real resources

External JSON, typed values, container payloads and reference expansion receive
an estimated allocation allowance derived from available physical RAM. It is a
read/decode guard, not a per-edit or document-size quota. The current policy
reserves part of available memory for the system and gives decoding half the
remaining headroom; unavailable telemetry uses a conservative fallback.

The container checks declared lengths before fetching large blobs, charges
copies and map/vector allocations before creating them, validates array/object
counts against remaining encoded bytes before reserving memory, and charges
every expanded reference. A small file with many references therefore cannot
silently expand without accounting for the resulting memory. Deterministic
tests can provide an explicit read allowance.

`DOCUMENT_READ_RESOURCE_PRESSURE` leaves the destination document unchanged.
The caller can free memory or retry on a machine with sufficient available
resources. JSON/binary nesting depth 64, integer-overflow checks, canonical keys,
exact SQLite schemas, payload identities, checksums and geometry validation
remain enforced.

Optional saved geometry retains its existing limits: 64 MiB per entry, 256 MiB
decoded total, 128 MiB compressed total and 8192 entries. Damaged or over-capacity
derived data can be omitted/discarded with a receipt and recomputed from the full
authoring. It never supplies permission to drop authored solids or history.

Transport limits also remain separate: the MCP/live request bound, live reply
bound, individual image limits and supported geometric-tool contracts are not
raised by this work. Use path-based import/save/load and compact inspection
where appropriate.

## Verified local scope

Synthetic cases cover a valid document with three distinct 700 × 700 PNG assets
whose combined source exceeds 4 MiB, small revision-checked edits, save/reopen,
exact source/asset/geometry comparison and retained history. Individual image
and command payloads remain below their existing limits.

Container cases cover legacy v1/v2, conditional v3 upgrade, unchanged IDs/data,
values above 16 MiB, more than 8192 slots, old history/replay thresholds, bounded
hostile expansion, sparse physical files above the former file ceiling and
transactional migration failure. Existing disk-full, interruption, corruption and
unknown-schema checks remain part of the regression scope. Test outcomes and
limitations are recorded in the iteration evidence; no general
latency, unlimited-project or manufacturing qualification is claimed here.
