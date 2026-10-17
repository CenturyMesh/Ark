# Persistent memory for ARK agents

040D repairs the original startup call identifier to fit the Responses
64-character limit. Exact legacy 039D host pairs are recovered only in the
outgoing request; stored history is preserved. Other malformed IDs are rejected
with a safe field path before transport. See [040D verification scope](iterations/040D.md).

039D addresses **A.03/A.04/A.05/D.02/D.05** with a native local memory system.
It complements Graphify's code navigation, the live CAD `context_graph` and
Obsidian's derived views. No external memory account, API key or paid inference
is required. Initial dependency preparation downloads pinned model/runtime
files; actual recall and maintenance run locally.

## Retrieval and lifecycle

SQLite WAL transactions persist attributed records across processes and app
sessions. Every record has a stable key, memory revision, original content,
source fingerprint and optional code anchors. Project scopes are isolated;
the explicitly selected shared scope holds reusable ARK knowledge. Actor names
are declared attribution, not authenticated identities.

The local semantic provider runs a pinned multilingual DistilUSE model with its
original cased WordPiece tokenizer, masked mean pooling, official Dense/Tanh
projection and normalized 512-component output. Long records use overlapping
windows rather than silently dropping the tail. Search combines actual cosine
retrieval, literal FTS5, recency and useful/unhelpful feedback. Engine availability,
model fingerprint and missing/reindex-required vectors are explicit. An absent
model never produces invented embeddings or a false semantic success.

Records may describe decisions, failures, procedures, evidence or episodes.
New scratch episodes remain review candidates. Successful or rejected agent CAD
operations enqueue small scratch receipts in a background worker: operation
names, outcome, source hash and observed revision. Geometry and conversations
are not copied. Queue acceptance is not durability; committed, failed and
queue-rejection counts remain visible. The worker does not block CAD evaluation
with embedding inference. Promotion and semantic reindexing are explicit.

Startup recall supplies bounded current-project context, including labelled
scratch candidates, plus curated shared knowledge. It is an actual executed host tool-call/result pair, never a new user message
or static onboarding instructions. Equal receipts do not repeat
their text, while a changed project/receipt is refreshed. Original user input and
host history remain retained. Read-only reviewers can recall and propose; writable
roles record/promote knowledge. Memory operations do not advance CAD revisions.

Consolidation flags contradictory declared subject/predicate/value facts and
proposes procedures from at least two distinct explicitly successful episodes
with identical step sequences. It preserves the original records and does not
rewrite history, certify facts or automatically promote a proposal. Feedback,
promotion, reindexing, consolidation and compression use observed memory
revisions and atomic compare-and-swap rejection.

Cold-vector compression stores signed four-bit values with a measured cosine
loss bound, retaining the original text and model identity. Reindexing can compute
fresh float32 vectors from that text. Compression is lossy and may affect ranking;
its byte saving applies to the vector payload, not the whole database.

Expiry marks/excludes stale context; it does not delete it. Explicit forget removes
live body/vector/search data and retains a revision/attribution tombstone to
prevent stale resurrection. Earlier exports and backups are separate artifacts.

## CAD and coding access

CAD agents have three tools:

- `memory_context`: wakeup/search/get/stats in project, shared or bounded combined scope.
- `memory_record`: store/forget/promote/feedback with memory revision receipts.
- `memory_maintain`: preview/apply consolidation, reindex and measured compression
  of explicitly selected records.

Current CAD identity selects its scope; unsaved projects without an identity have
ephemeral discovery and must establish identity before project-specific writes.
Historical source hashes are caller declarations unless current evidence is
actually supplied and checked. A matching memory revision never authorizes a CAD
write. Native B-rep, current revision and inspection remain authoritative.

Coding agents can use the same store without running Studio:

```powershell
powershell -NoProfile -File scripts/project-memory.ps1 -Action wakeup
powershell -NoProfile -File scripts/project-memory.ps1 -Action search -Query 'revision conflict'
powershell -NoProfile -File scripts/project-memory.ps1 -Shared -Action search -Query 'native loft'
powershell -NoProfile -File scripts/project-memory.ps1 -Action store -ArgumentsFile note.json
powershell -NoProfile -File scripts/project-memory.ps1 -Obsidian
```

The wrapper selects the repository scope from its origin (or common Git directory)
and accepts an explicit `-Project` for a host-selected workspace. `-Directory`
selects a separate memory root for tests. `ARK_MEMORY_DIRECTORY` overrides the
default per-user `ARK/memory` directory; `ARK_MEMORY_ASSETS` selects a prepared
semantic model directory. These are process/host settings, not tool arguments.

A memory-only MCP endpoint avoids advertising unrelated CAD tools:

```text
ark-mcp --memory-project code:your-workspace-key
```

Use the same explicit project key with the CLI wrapper when sharing that namespace.
Any local MCP client can call the three memory tools. No global client config is
rewritten automatically.

## Obsidian and evidence

Native export preserves a fresh scoped snapshot. The optional script bridge
creates a new combined vault from verified code notes plus that snapshot, checks
source anchors and links, and atomically publishes its own current pointer under
`out/context-memory`. Original code and personal vaults remain untouched. Memory
is not ingested into the Graphify source index and no drawing/session scan occurs.

The independent semantic reference checks all components of four vectors, with
observed maximum error 5.96e-8. A small fixed quality pilot ranked the target first
for four English questions and two of three Portuguese questions; the remaining
Portuguese target ranked second. q4 retained these measured ranks while reducing
only those vector bytes eightfold. These are bounded synthetic controls, not a
claim of universal recall, equivalence to every memXT feature, whole-database
compression or matched provider-token savings. Final integration evidence is
recorded separately for this iteration.
