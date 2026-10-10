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
