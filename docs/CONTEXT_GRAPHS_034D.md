# Shared project context and local code graphs

035B extends this contract with authored cable endpoints/occurrences, PCB
components/scoped net labels, supplier URLs and stored STEP retrieval metadata.
See [035B](iterations/035B.md). The source-derived code graph remains separate
from live CAD authoring. Four curated guides in `docs/knowledge` are exported
under `ARK Knowledge` with actually resolving code-note links, without semantic
model calls. Code-graph and knowledge hashes jointly version each vault.

034D / 0.9.35 addresses A.01, A.03, A.04, D.02 and D.04. The project graph is
native read-only ARK code. The optional repository graph uses pinned
[Graphify CLI](https://github.com/Graphify-Labs/graphify), `graphifyy==0.9.92`.
Neither requires a vector database or a model call to build its code/authoring
index. Graphs support discovery; authored inputs, B-rep and producer contracts
retain geometric authority.

## Project graph available to every team role

`context_graph{query:"suspension",depth:1,limit:12,history_limit:3}` returns
matching nodes and a bounded neighborhood. Use exact `keys:["feature:42"]` after
discovery, instead of `query`, to retain the intended target. Nodes cover features,
parameters, sketches, planes, materials, assembly nodes and mates. Installed
[parametric program](PARAMETRIC_PROGRAMS.md) bindings are `program:<namespace>`
nodes with the program name, `namespace`, `program_sha256` and resolved `values`,
and `owns` links to their uniquely named owned features, sketches, datums and
materials; their values are not scanned as document parameter names. Directed links
come from explicit selectors or expression identifiers; both dependencies and
backlinks can be traversed. This is not complete evaluator dependency analysis.
A repeated read in the same session reuses the index when an authoring stamp
matches the saved source. That read does not rebuild the authoring JSON or the
graph. The stamp follows the saved authoring fields, so an in-place edit at the
same revision still rebuilds. `source_sha256` remains the hash of that JSON, and
the returned neighborhood stays bounded.
The stamp retains the saved JSON's full numeric precision, length-delimited
values and explicit collection boundaries. Small density/opacity edits cannot
alias through six-decimal formatting. Serialization diagnostics use an atomic
counter; independent sessions keep their own cache, while a host must still
serialize calls to the same session. The stamp scans saved fields and serializes
some subobjects, so avoiding the full JSON is not constant-time or a measured
provider-token saving. Future persistent fields must join the stamp contract.
Program bindings and saved design variants are part of the stamp, so a detach,
which changes only the binding, or a variant edit rebuilds the index.

The response includes project `scope`, observed `revision`, saved-authoring
`source_sha256`, node fingerprints and explicit `partial` status. Depth is 0..2,
selected nodes 1..64 and history rows 0..8. A truncated selection does not prove
there are no other relevant nodes. Query words match names, kinds and compact
facts; exact selectors reject missing or repeated keys. Geometry buffers stay
local. Use `object_context` and native inspection for actual selected geometry.

078B adds deterministic continuation without increasing response limits.
`direction:both|outgoing|incoming` filters traversal by the stored edge direction;
it does not reinterpret every relationship as a geometric dependency. `page`
reports matching roots, the requested neighborhood size and `has_more`.
Call `context_graph` with the returned `next_call` arguments for the next page.
Its cursor binds project scope, saved-authoring SHA-256, revision and exact
query/keys/depth/direction/limit/history selection. Any mismatch rejects and
requires fresh discovery, including an in-place source edit at the same revision.
Usage, audit and journal bookkeeping are excluded from that source projection.
An agent-attributed first read or read batch establishes its activity identity
before returning scoped continuation, then records the normal single action.
Its own audit/feedback updates do not immediately invalidate the returned cursor.

`frontier` and `boundary_edges` retain omitted incident neighbors even when known
fingerprints omit unchanged node facts. The frontier excerpt is bounded to64
nodes; internal `edges` and `boundary_edges` share a128-edge page, with exact
counts and offsets. Edge reporting includes **all incident edges** of the node
slice, independent of the direction used to traverse nodes. This prevents a
root-to-leaf edge from disappearing when only its leaf is on a later page.

`next_call` drains remaining edges before advancing to the next node slice.
Its optional cursor `edge_offset` is bound to that slice and the same source,
revision and selector. Edge-only continuations have empty `nodes` and preserve
the earlier node facts; edge identities can overlap across endpoint slices.
Aggregate both edge arrays and deduplicate identities. `page.has_more` and
`partial` include pending node **or edge** chunks; `nodes_have_more` and
`edges_have_more` distinguish them. No page claims unbounded edge transport.
`expand_call` expands from
the whole current page, so its own continuation can reach neighbors beyond that
excerpt. `page.depth_boundary_nodes` separately describes an unexpanded radius.
Neither `partial:false` nor a completed page proves full dependency closure.
Feature rows expose observed/conservative `geometry_domain` even in delta reads;
the domain is not geometric or manufacturing certification.

Supply the received `known_scope` and `known_fingerprints` to omit unchanged
node facts. Fingerprints cover that node's authored data and incident links;
they do not certify its evaluated geometry or an unchanged transitive dependency.
An edited parameter can therefore change its own fingerprint while leaving a
connected feature's authored fingerprint unchanged. Revisions still govern writes.
Known fingerprints from another scope reject explicitly. `expected_revision`,
when supplied, must match. `query_batch` supports the same read-only tool.

Parallel roles use the host's serialized shared session/index, not independent
copies of project state. A successful peer edit invalidates the index by saved
source hash before the next read. Roles still receive their own relevant result
and pay any provider input cost for that result. Cross-role cache reuse avoids
duplicate index work; it is not cross-provider prompt-cache sharing. Do not pass
another role's fingerprints unless that role also supplies the associated facts.
Focused remains the UI default and Parallel an option, without new turn/token
execution budgets.

Relevant errors, self-feedback, wished tools/options and BOM pending notes come
from existing activity records, with attribution, revision and capture gaps.
These are historical receipts and attributed statements, not a declaration that
a complaint is still unresolved. Reinspect current evidence. Saving the existing
project authoring/activity preserves those sources; reopening reconstructs the
graph. There is no new `.ark` format or external upload, and no new freeform
cross-project memory store. Unsaved projects without a persistent identity use a
source-hash scope, which deliberately changes when their authoring changes.

## Request representation

The HTTP assistant path can deduplicate old successful `context_graph` exchanges
even below the general 96 KiB projection threshold. A newer receipt must cover
every old node/fingerprint/fact, edge and history row at the same project scope
and source hash. Delta facts can be restored only from exact prior
scope/key/fingerprint matches. An unrated action stays exact until a successful
`tool_feedback` receipt contains its ID. Different sources/scopes, incomplete
call/output pairs, errors, writes and candidate receipts are not deduplicated.
078B also retains distinct page, frontier, boundary-edge and continuation
receipts. Identical node facts alone cannot erase a route to omitted context.

Only the outbound request changes, and only when its actual serialized size is
smaller. Full host history and exact user/role messages remain. The continuation
guard requires a fresh successful `doc_info` before another write, and original
call IDs still prevent replay. Existing large-history projection is a separate
layer. ACP provider conversation storage is outside this HTTP request projection.

## Repository graph and Obsidian

From the repository root, with `uv` installed:

```powershell
powershell -NoProfile -File scripts/code-context.ps1 -Prepare
powershell -NoProfile -File scripts/code-context.ps1 -Query 'projectContextGraph'
powershell -NoProfile -File scripts/code-context.ps1 -Query 'projectContextGraph' -Context call
powershell -NoProfile -File scripts/code-context.ps1 -Obsidian
```

Preparation downloads the pinned CLI and dependencies once. Following calls use
`uvx --offline`, `GRAPHIFY_BACKEND=none`, `--code-only` and no semantic extraction.
The wrapper restores the caller's environment. Query logging is disabled.
Only allowlisted source extensions under `src`, `include`, `tests`, `scripts`
and `cmake` enter an owned snapshot. User CAD files, sessions and environment
files are excluded. The optional tool is not bundled into the ARK runtime.

New exports include `ARK Context.md` and `.ark-context-navigation.json`, bound
to the same code/graph fingerprints as the vault. They link the actual canvas,
available code cards and curated guides, and report files/nodes without source
locations and relation-confidence counts. This is location coverage, not a claim
of complete parsing or dependency analysis. Old edited exports remain untouched.
The combined memory exporter now rejects a selected code vault whose source/graph
differs from the current code-index manifest, and guards that manifest during
publication. Refresh with `code-context.ps1 -Obsidian` before combining memory.

The source manifest hashes each input and the generated graph. Changed/deleted
inputs or a changed graph invalidate reuse. The wrapper conservatively performs
a full extraction after source changes, verifies source did not change during
extraction/query and uses an exclusive index lock. An unchanged query reuses the
index. `-Refresh` forces extraction. Exit 3 means ambiguity and exit 4 no match;
both need an explicit narrower query/source inspection. `-Seed` can narrow a
query according to the CLI's source/symbol selectors.
`-Context call` filters to call edges when common type/reference hubs make a
neighborhood too broad. Truncation remains explicit; narrow the symbol/context
instead of assuming the omitted portion has no relevant nodes.

Generated graph and vault live only under `out/context-code`. `-Obsidian` exports
linked Markdown notes and `graph.canvas` into a separate directory per graph
hash under `out/context-code/obsidian`. The script prints the exact vault path;
`out/context-code/obsidian-current.json` points to the verified current export.
Every canvas file card must resolve before that pointer is updated. Earlier
vaults remain untouched; an incomplete export uses a new directory on retry.
This avoids upstream Windows pruning of case-renamed notes during re-export.
This uses Graphify's exporter and Obsidian's
[linked-note graph](https://obsidian.md/help/plugins/graph) convention. It does
not modify an existing personal vault or install Obsidian. CAD project graphs
are read through ARK tools; this export covers the repository code graph.

C++ AST extraction has coverage limits: declarations, macros, included fragments
and parser errors may yield file nodes or partial symbol sets. Inspect warnings
and confirm every implementation decision in current source. No graph match is
not proof of no dependency, and there is no general whole-program call proof.

## Measurement and qualification

See [034D evidence](iterations/034D.md). Public synthetic fixtures compare the
same selected feature and the same repeated receipts, retaining failures and
constraints. Serialized bytes are measured separately from provider tokens.
No paid model evaluation, live token percentage, design-quality gain, general
manufacturing qualification or hosted deployment is established by these tests.
