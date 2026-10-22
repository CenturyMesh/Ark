# Recoverable history compaction — 051A

Milestone **A.02**. Implementation and synthetic verification targets are present;
execution evidence is recorded in the iteration verification notes after running them.

`history_compact` is an explicit operation for a long-running drawing. ARK still
rejects an edit beyond 512 retained nodes or the 4 MiB metadata limit. It never
prunes history automatically. The tool requires an observed `expected_revision`,
`confirm:true`, `keep_recent` (integer 0–256), and a new `archive_path` ending in
`.ark`. Optional actor and motivation are caller-declared provenance.

ARK writes the **complete current document**, including unsaved authoring, all
history branches, usage and activity, to private staging beside the archive. The
normal document writer preserves retained native geometry and optional derived
sections. ARK reopens the staging archive and compares its complete logical
document with the original. It validates and reopens the prospective compacted
document before publishing the archive with a no-replacement operation. Only then
does it replace the in-memory document and advance revision. Stale revision,
invalid retention, occupied destination, save/reopen failure or retained-capacity
failure leaves the current document unchanged. An existing archive is never replaced.

Retention follows the current head's **ancestor path**, keeping at most
`keep_recent` nodes plus the state preceding that range as its baseline. Geometry,
authoring and current receipts are unchanged. Node ids and before-state hashes are
recomputed for the retained chain; node metadata also identifies its original id,
parent and previous active head. Other branch heads and older states remain in
the archive. Optional `retain_ids` explicitly preserves other selected iteration
states as separate nodes based on the new baseline, without changing current
authoring. Ancestors plus selected nodes may total at most 256; duplicate selected
ids are kept only once. The receipt maps every selected original id to its new id.

Undo follows previous active states, which can cross branches. Compaction retains
only the contiguous newest suffix of the undo stack whose targets are available
in the retained range. It stops at the first missing target; it never jumps over
that state. Consequently, undo may be empty immediately after restoring a branch.
Redo is explicitly cleared. Undo and redo generated after compaction work normally.
`keep_recent:0` makes the current authoring the baseline and clears both stacks.

`history_info.compaction` and the tool receipt report retained node/undo counts,
cleared redo count, other branch heads archived, original-to-current retained ids,
and archive path, binary file SHA-256, logical document SHA-256 and verified size.
The receipt is fingerprinted for corruption detection, not authenticated identity.
Only the latest receipt is kept in the active journal; each complete archive
contains the previous receipt, forming a recoverable archive chain without an
ever-growing receipt list. Archives must be kept with the project. Their recorded
paths are local references and are not fetched automatically on document load.

Compacted documents require document format **50**, so older readers reject them.
The existing journal delta encoding remains schema 1 or 2. A paired compaction
marker and validated receipt are mandatory; unsupported versions, missing receipts
and altered receipt fingerprints fail closed. This version change is for the new
persistence contract, not the application release label.

Save the result using `doc_save`. Restore the complete archive using `doc_load`
or Studio **File → Open**. An archive at its old 512-node bound can be inspected
and saved immediately; compact a copy explicitly before recording further edits
or undo operations. To recover an arbitrary old branch state from a full archive:
load the archive, inspect `history_info`, compact with `keep_recent:0` and
`retain_ids:[selected_id]` to a new archive path, then use `iteration_restore` with
the mapped id from `compaction.retained_ids`. The selection is explicit and the
current design remains unchanged until that restore. Studio
**Iterations → Archive and compact…** shows current
capacity, asks for archive and retention, explains bounded undo/cleared redo,
confirms the operation and saves the result. The history details show the exact
archive path for recovery. If the subsequent save fails, Studio reports the
in-memory result and the archive location; the verified archive remains intact.

Verification targets:

- `history-compaction-051a`: synthetic project with 601 successful authoring edits,
  compaction at 512 nodes, continued edits, repeat compaction, binary save/reopen,
  all retained undo targets, redo after undo, archive restore through `doc_load`,
  branches, selected off-path intermediate state recovery from a full 512-node
  archive, duplicate/unknown selections, zero retention,
  stale/no-confirm/archive failure/no-overwrite and
  corrupted/missing receipt rejection.
- `iteration-history`: existing journal integrity, capacity, undo/redo, ownership
  of usage receipts and atomic rejection contracts.
- `studio-history-compaction-ui`: synthetic Studio control exercise with decline,
  confirmation, saved archive and compacted reopen, and before/after captures.

These are local persistence/UI checks. They do not qualify manufacturing geometry,
external deployment, archive storage availability or protection against external
deletion. Native geometry remains governed by its existing producer contracts.
