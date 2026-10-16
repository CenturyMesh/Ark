# B.04 — selective evaluated geometry and isolated configurations

029B keeps **B.04 Parcial**. The implemented scope is selective evaluation of
authored assemblies and revision-bound isolated container configuration work.
This is local runtime evidence, not a claim of manufacturing qualification or
unbounded assembly capacity.

## Selective loading

`assembly.load` selects assembly node subtrees. Source definitions, occurrences,
constraints and physical BOM quantities remain authored. Its dependency closure
follows explicit feature inputs, named sketch and datum chains, projected source
features/occurrences, native placed inputs and supported pattern source fields.
Skeleton-expanded sources use the same explicit graph after source refresh.
Missing/cyclic semantic dependencies reject; no tolerance is widened.

All mate endpoint definitions remain available to the exact assembly solve.
All active patterns retain their semantic guide, point, linked-pattern and
alternate sources so an unselected pattern still contributes its full physical
occurrence inventory. This can retain additional geometry beyond selected
subtrees. A simple pattern does not require every unrelated definition to load.

After successful recomputation, excluded body/cache owners and auxiliary native
reference/centroid caches are released. Native source payloads embedded in
authored recipes remain intact. The `.ark` source package is still read and
parsed as a complete authored document; selective evaluation does not promise
streaming document deserialization or a proportional reduction in process RSS.

Every external pin is validated against its authored imported source hash.
Only loaded pins read and hash their external files. `assembly_authoring_info`
marks the others `deferred_until_loaded`, with explicit verified/deferred counts.
Reload reads the pin again; changed/missing source rejects the transaction while
retaining the previous selection and authoring. `assemblyExternalIo()` reports
cumulative actual file open/read/byte observations for verification fixtures.

## Container ownership

New checkouts carry `ark.design-container.v2` and
`selection_policy: "workspace_local"`. These semantics require source format
41; ordinary documents retain the format required by their actual constructs.
Legacy v1 checkouts remain readable with their original restrictions.

Configuration ownership is recorded per recipe in the persisted editable or
readonly manifest rows. Parameter targets and visibility node IDs determine its
scope. A wholly outside recipe stays in the parent. A recipe mixing retained
and outside targets, or depending on source absent from the checkout, rejects
with `CONTAINER_CONFIGURATION_BOUNDARY`; split that recipe explicitly first.
Recipes targeting shared readonly source are themselves readonly. Applying one
cannot modify a protected parameter or occurrence.

External pins follow ownership of the imported feature. Scoped pin changes use
the same source hash validation and explicit revision receipt. New occurrence
visibility keys and feature pin IDs remap with the merge ID map. Outside source
recipes remain in the parent and participate in its source digest.

Child load selection is independent: checkout initially selects its container
root, and later local selection changes never replace the parent's load roots.
All dependency definitions remain authored and can be explicitly reloaded.
Global history rollback and global Skeleton ownership remain typed rejection
boundaries, as do crossing mates, shared definitions and unsupported appearance
ownership in the existing container contract.

Merge preview and publication retain `expected_revision`, source digest,
baseline digest and reviewed child digest. Publication reuses the normal atomic
`doc_apply` validation. No silent rebase or global source takeover is performed.

## Verification

`ark-configurable-assembly-tests` covers a 3,000-occurrence analytic fixture,
semantic dependency closure and missing/cyclic source rejection.
`ark-design-container-tests` covers configuration ownership, readonly application,
mixed-scope/expression rejection, local selection isolation, recipe removal/new
occurrences, source-format downgrade and persisted provenance, alongside existing
stale revision/source/child hash and atomic rejection cases.

`ark-assembly-scaling-native-tests` creates eight exact two-solid STEP sources
and 320 occurrences in an isolated temporary directory. Its warmed full and
selected recomputes use identical authored inputs and report actual pinned-file
bytes read, retained native JSON payload bytes, deduplicated mesh vector capacity
bytes and expired excluded cache owners. It additionally exercises real imported
pin/configuration binary roundtrip, guarded merge and atomic source-drift reload.
Native JSON payload length is an explicit retained-data measure, not allocator
overhead or an operating-system memory estimate. Timing and total RSS reductions
are not inferred from these checks.

Local Windows/OCCT execution passed on 2026-10-15. The native fixture measured:

| Observed quantity | Full | Selected |
| --- | ---: | ---: |
| Evaluated/cache definitions | 8 | 1 |
| Pinned source files actually read | 8 | 1 |
| Pinned source bytes actually read | 182,292 | 22,776 |
| Retained native JSON payload bytes | 1,229,320 | 152,588 |
| Mesh vector capacity bytes | 253,440 | 31,680 |
| Exact reference cache payload bytes | 112,154 | 13,995 |

Seven excluded cached body owners expired. All 320 physical occurrences and all
eight authored exact sources remained present; the selected source retained both
of its solids. Real native pin/configuration binary readback, reviewed merge,
outside-pin preservation and failed-drift atomic reload also passed.

Two earlier native fixture attempts failed in test assertions: the first
compared JSON object insertion order across binary persistence, and the second
looked for faces outside the published `topology.references` inventory. The
corrected checks compare canonical source semantics and consume real named
reference rows. See the [machine-readable receipt](verification-assembly-scaling-029B.json)
for the complete attempt accounting. These deterministic data/custody counts
do not establish a general latency, total process memory or design-cost gain.
