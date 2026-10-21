# Persistent drawing iterations

Successful authored changes through `AgentSession` create immutable journal
nodes in the `.ark` file. This includes batches, imports, prepared commits,
human Studio edits, undo, redo and explicit restoration. Failed geometry and
required conditions create no node. Read-only probes and previews do not
create iterations. A no-op batch can advance the session revision without
adding a node because its authoring state did not change.

Version 27 stores a first observed baseline, structural authoring deltas and a
SHA-256 dictionary for strings of at least 1024 bytes, including imported STEP
and B-rep payloads. The current authoring also references that dictionary.
The journal object carries `schema:1`; only an explicit 049A migration writes
`schema:2` (see below).
Evaluated meshes, caches, journal snapshots and token ledger snapshots are
excluded. Older versions through 26 remain readable; their first mutation
captures the loaded state as baseline. Earlier unsaved actions are unknown.
`doc_load` adopts the loaded journal and `doc_new` starts an independent
project; neither carries the prior project's geometry into the new file.

Each node has `id`, `parent_id`, `previous_head_id`, `ordinal`, `actor`,
`provenance:"caller_declared"`, `tool`, a bounded `summary`, optional supplied
`motivation` (empty when omitted), `utc`, `before_sha256`, `after_sha256` and an
authoring `delta`. Restore nodes additionally identify `restored_from_id`.
Actor fields are `kind`, `id` and optional `role` and `model`, with 1..256 bytes
per field. They record what the caller declared and do not authenticate an
identity or a provider. No reasoning transcript is retained or inferred.
SHA-256 detects accidental corruption and inconsistent file edits; it is not
a signature and cannot authenticate a maliciously rewritten entire journal.

`history_info{limit?:1..64,before_id?:string}` returns newest metadata first, `head_id`,
`baseline_id`, `nodes`, `node_count`, `branch_heads`, `storage_bytes`,
`document_bytes` and `max_document_bytes:4194304`. It returns no large geometry
payload. `has_more` and `next_before_id` expose bounded older pages; pass the
cursor as `before_id` to exclude that anchor. Cursors stay stable under new
commits, and unknown cursors reject. Empty legacy documents return empty identifiers and lists. The
baseline can be restored by its identifier even though it is not a node row.
`doc_get` returns the unpacked current document and usage, plus a compact
`history_summary`; it does not send the old journal into an agent prompt.

```json
{"tool":"iteration_restore","arguments":{
  "id":"iteration-<sha256>","expected_revision":12,
  "motivation":"Return to the last accepted dimensions",
  "actor":{"kind":"human","id":"studio"}
}}
```

Restoration re-evaluates geometry and creates a new node whose parent is the
selected iteration. `previous_head_id` records the previously active node;
the previous branch remains available. The session revision and feature,
assembly and mate allocators stay monotonic, while current token receipts are
preserved. `doc_undo` and `doc_redo` use persistent target stacks after reopen,
also writing new immutable nodes. Successful mutation receipts include
`iteration_id` and `head_id`; refresh `history_info` after receiving one.

The full persisted file, including current authoring, journal and usage, is
limited to 4 MiB. There are at most 512 nodes, and validation/replay is bounded
to 131072 delta operations. An individual reconstructed authoring state is
also bounded to 4 MiB before repeated blob references can expand memory.
Missing references, duplicate/cyclic nodes, changed node hashes, changed
payload hashes or a mismatched current head reject loading atomically.
Authoring commits verify that the same journal can reopen before acceptance.
Capacity failure restores document, revision, undo/redo and prepared candidate
custody; it never silently deletes old nodes. An oversized save leaves the
existing target file intact. External exports are outside drawing history.

## Capacity visibility — 045A

`history_info` also returns `max_nodes:512`. Because reaching either limit
rejects every further edit, commit receipts add `history_capacity`
(`node_count`, `max_nodes`, `document_bytes`, `max_document_bytes`, `warning`)
once the journal reaches 75% of the node limit or the document 75% of 4 MiB.

Undo and redo also write nodes, so a temporary state committed and then
reverted costs two nodes. When a `doc_apply` made only of `assembly.drive`
commands returns the assembly exactly to its state before the previous
iteration, the receipt adds `history_round_trip` (`returned_to_before`,
`nodes_used:2`, `hint`). Detection compares exact authoring digests: the first
drive replaces an authored placement with the solved one, and a return that
differs by floating-point drift is not reported. Look at temporary poses
read-only instead:
`assembly_info{drive|sweep}`, or `doc_preview` with `assembly.drive` followed
by `snapshot{candidate_id,expected_revision}`. Both nodes of a round trip stay
in the journal; no node is merged, rewritten or deleted.

## Derived baked frames — 049A

Baked-motion frames are derived data and never enter the journal. A bake stores
a receipt with `frames_sha256` and `coordinates_sha256`; the frames live in
memory and the optional derived section ([derived bakes](DERIVED_BAKES_049A.md)).
Re-baking at a higher fps therefore no longer grows the journal.

Documents written by 044B-048B kept inline frames in the authoring, so every
re-bake added a new copy to the blob dictionary. Opening such a document
changes nothing: authoring and journal stay byte-identical and the frames stay
readable. The next `motion.bake` of a motion writes a receipt; that node's
delta erases the payload keys and adds no frame bytes. Older nodes keep theirs.

`history_info` reports `schema`, `migrations` and `inline_bake_payloads`
(`references`, unique `bytes`, and a hint while any remain). A commit rejected
for capacity adds `derived_bakes.inline_bake_payload_bytes` when old frames are
present, and the 75% `history_capacity` warning adds
`inline_bake_payload_bytes`.

### Explicit migration

```json
{"tool":"history_migrate_derived","arguments":{
  "path":"C:/designs/kiosk.ark","expected_revision":41,"confirm":true,
  "motivation":"Reclaim derived frames","actor":{"kind":"human","id":"studio"}
}}
```

Run it only on the user's explicit instruction. It rejects with
`HISTORY_MIGRATION_CONFIRM_REQUIRED` without `confirm:true`,
`HISTORY_MIGRATION_PATH_REQUIRED` unless `path` is an existing saved `.ark`,
`REVISION_CONFLICT`, `HISTORY_MIGRATION_NO_JOURNAL` and
`HISTORY_MIGRATION_NOT_NEEDED`. Every rejection leaves the document and its
revision unchanged. Then:

1. The complete current document (authoring, journal, usage and activity) is
   saved beside `path` as `<name>.pre-049A.ark`, or `-2` up to `-99` when that
   name exists. It is written to a staging file and published without replacing
   any file, then reopened and compared with the session document. Failure
   stops here (`HISTORY_MIGRATION_BACKUP_*`).
2. Every authoring state (baseline, every node, every branch) is replayed and
   every inline bake becomes a receipt; a state with a receipt selects format 49.
   Deltas are recomputed from the converted parent and child states, the blob
   dictionary is rebuilt from what they reference (frame blobs disappear), and
   `before_sha256`, `after_sha256`, the baseline id and node ids are recomputed.
   Parent, previous head, restore, head and undo/redo references are remapped.
   Actor, tool, summary, motivation, UTC time and ordinal of every node are kept.
3. The journal becomes `schema:2` and gains a `migrations` record:
   `kind:"derived_bakes"`, actor, caller-declared provenance, motivation, UTC,
   previous and new head and baseline ids, node and state counts, payload
   references and bytes, blob counts, journal and document bytes before and
   after, and the backup path, size and verification. The record is not part of
   any node hash; like undo/redo stacks it is checked for shape, not signed.
4. The rewritten journal is validated and the migrated document reopened in
   memory; only then does it replace the session document and the revision
   advance by one. Prepared candidates are discarded. The current frames stay
   cached for playback.

The tool does not save. `doc_save{path}` writes the migrated document and its
frames to the derived section. Studio's **Persistent iterations** dialog offers
**Reclaim baked frames** while inline frames remain; it states the backup name,
asks for confirmation, runs the tool and saves to the open document's path. A
document that was never saved is refused.

Node ids change. Agents must re-read `history_info` before `iteration_restore`.
Restore, undo and redo work on the rewritten journal.
