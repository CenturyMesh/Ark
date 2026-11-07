# Rebuilds, context and long-term memory — 069D

**A.02/A.03/A.04/B.02/D.02/D.05 — Verificado no escopo local**, with evidence
in [069D](iterations/069D.md). Source changes and CAD model changes use different
paths; this iteration addresses both without changing the document format.

## Model evaluation

Exact sketch projections previously rebaked after every published feature,
including unrelated features, even when their complete source closure was
already available. A recompute now retains successful projected sketches within
that one pass after checking their feature and sketch ancestors. Waiting,
failed, unknown and occurrence-dependent closures retain the conservative path.
Every edit, undo, redo and reopened document starts a fresh pass.

Pole deformation, boundary matching, multi-face trim and trim-preserving
extension now use their audited explicit input dependencies for cache reuse.
Editing an unrelated earlier feature no longer invalidates those exact results.
Source and target B-rep hashes, expression values and actual dependencies still
invalidate them; stale Match/deformation contracts reject instead of rebasing.
Curve networks and unsupported closures retain their conservative dependency
path.

`doc_info` reports `projection_builds` and `projection_reused` alongside the
existing feature and timing counters. These counters are transient diagnostics.
Native validation, expected revisions and atomic publication remain required.

## Context transport

The outgoing HTTP request shares exact repeated successful inspection receipts.
The first complete receipt remains in that same request; later equal receipts
refer directly to it and retain their own call IDs, revision and feedback data.
This happens after history selection, so a reference never points to a discarded
receipt. Original host history and actual tool execution remain complete.

Only an audited list of read tools participates. Mutations, unknown tools,
errors, candidate plans, result handles, memory, images and delta reads are
conservative boundaries or exclusions. Changed measurements and unknown fields
prevent equality. There are no new agent turn or token limits. Fewer serialized
bytes are transport evidence; provider token use, cost and task quality need a
matched live-provider comparison.

## Persistent memory in two stages

SQLite project memory, lexical/semantic recall, attributed episodes and explicit
promotion already persist across application sessions. 069D adds two read modes:

```json
{"operation":"search","text":"surface matching","detail":"index"}
{"operation":"get","key":"observed-record-key","detail":"full"}
```

Use these with `memory_context`. The index returns title, kind, outcome,
freshness, source and a recovery selector without copying prose, facts or steps.
Its fingerprint is specific to that representation: reading an index cannot
silently suppress the first full recall. Normal full reads retain their existing
contract. Index bounds that cannot fit one complete row reject explicitly.

For selected records whose memory revisions have been observed:

```json
{
  "operation":"distill",
  "keys":["episode-a","episode-b"],
  "expected_revisions":{"episode-a":1,"episode-b":2}
}
```

The result shares byte-identical content, facts and ordered steps in `extracts`.
Join each record's `extract_id` to recover those fields. Every source,
attribution, outcome, expiry, revision and freshness result stays with its own
record. Conflicting structured declarations are reported separately. This is
lossless extraction and deduplication, not an inferred semantic summary or
model-weight distillation. Distinct content is not collapsed just because its
embedding is similar. A result that exceeds `max_bytes` rejects instead of
quietly dropping qualifications. It never deletes, rewrites or promotes memory.

The existing consolidation path can propose reusable procedures from repeated
declared successful episodes; those proposals still require explicit review and
promotion. Embedding compression concerns vector storage, not the accuracy or
length of original text. See [persistent memory](PROJECT_MEMORY_039D.md).

The CLI wrapper accepts `-Action distill -ArgumentsFile selection.json` and
`-Action search -ArgumentsJson '{"detail":"index"}' -Query 'surface matching'`.
Use an executable from this iteration through `-Executable` when another ARK
version is installed. No conversations, drawings or credentials are scanned.

## Graph and Obsidian

`scripts/project-memory.ps1 -Obsidian` retains its verified fresh-snapshot
workflow and now emits `memory-graph.canvas` and `ARK Memory/Recall index.md`.
The canvas connects memory records to unique code cards through explicit source
anchors. Green indicates that a current source hash matches the declared anchor;
amber marks stale declarations. Neither is a verification of the claim or of
geometry, and code cards remain snapshots of their original index.

All canvas file cards and edge endpoints are checked before the combined vault's
current pointer is atomically published. Existing code vaults and personal
Obsidian vaults are unchanged. The file format follows the official
[JSON Canvas 1.0 specification](https://jsoncanvas.org/spec/1.0/). Existing
`context_graph` remains the revision-bound CAD navigation path; the repository
graph and long-term memory do not replace native geometric authority.

## Source compilation

Keep one configured development directory and compile only the affected runtime
and test executables. The release artifact receives a new public identity;
creating an entirely new compiler tree is unnecessary for every iteration.
See [development build commands](DEVELOPMENT_BUILD_069D.md) for exact scripts,
configuration drift checks, source/binary receipts and the separate full
distribution validation path.
