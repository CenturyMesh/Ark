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
parameters, sketches, planes, materials, assembly nodes and mates. Directed links
come from explicit selectors or expression identifiers; both dependencies and
backlinks can be traversed. This is not complete evaluator dependency analysis.

The response includes project `scope`, observed `revision`, saved-authoring
`source_sha256`, node fingerprints and explicit `partial` status. Depth is 0..2,
selected nodes 1..64 and history rows 0..8. A truncated selection does not prove
there are no other relevant nodes. Query words match names, kinds and compact
facts; exact selectors reject missing or repeated keys. Geometry buffers stay
local. Use `object_context` and native inspection for actual selected geometry.

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
