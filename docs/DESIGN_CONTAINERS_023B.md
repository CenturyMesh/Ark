# Isolated design containers — 023B

029B extends this contract with scoped configuration/pin ownership and
workspace-local loading in container v2, selecting source 41. Legacy v1 remains
readable. See the current [assembly scaling contract](ASSEMBLY_SCALING_029B.md)
for the added boundaries and measured evidence.

Roadmap **B.04** and **D.03**: partial local scope. A container is an existing
assembly group (`AssemblyNode.feature == 0`). Its ordinary parent/child hierarchy,
part definitions, occurrence IDs and patterns remain the assembly authority.
There is no second assembly engine.

`design_containers` discovers groups, inspects their source ownership, checks out
an editable subset and integrates an edited subset through the owning session's
atomic `doc_apply`. A checkout is an independent document/session. Another Studio
instance or standalone MCP session can open its explicitly saved `.ark` file.
Opening a second instance does not establish an automatic live link or guarantee
process scheduling, RAM savings or a measured speed improvement.

## Source workflow

Create the group with ordinary revision-checked assembly commands. Place its
occurrences below that group; adding a group does not copy or convert geometry.

```json
{"expected_revision":12,"commands":[
  {"op":"assembly.add","name":"Frame module","feature":0},
  {"op":"assembly.edit","id":"Rail occurrence","parent":"Frame module"}
]}
```

Reparenting uses the existing authored local-placement contract. Preserve the
desired world placement explicitly when moving between different parent frames.
The Studio **Create design container** control groups its selected occurrence
under a new identity at the same parent, retaining that occurrence's local pose.

```json
{"action":"inspect","node":"Frame module"}
{"action":"checkout","node":"Frame module","expected_revision":13}
{"action":"checkout","node":"Frame module","expected_revision":13,
 "path":"C:/design/module-work.ark"}
```

Checkout without `path` returns the authored child `document` and retained
`manifest`. An explicit new `.ark` path publishes a binary child document without
overwriting an existing file. Publication uses an atomic exclusive hard link
from a completed sibling staging file; filesystems that cannot support this
operation reject explicitly. Ordinary `.ark` serialization and its capacity
limits still apply, including the **4 MiB logical document limit**. The retained
baseline adds source bytes to the child package; arbitrary large-project
qualification remains open.

The child includes the selected group and descendants, their part definitions,
ordered feature-input closure, referenced named sketches/datums/parameters and
materials, internal mates and the authored ancestor frames required for its pose.
Unrelated feature histories and sibling occurrences are absent: recomputation
runs on this actual subset and its retained dependencies.

The manifest declares each retained source as `editable` or `readonly`.
Definitions placed inside the group are editable. Definitions included only as
external feature inputs and ancestor frames remain read-only. Named resources
used exclusively by editable scoped source are editable; resources also consumed
outside the scope remain read-only. Source discovery conservatively follows
authored names, expression parameter tokens and explicit producer/occurrence
IDs. Ambiguous or unsupported boundaries reject; they are not approximated.

Existing authoring IDs remain unchanged in the child. New IDs start at the
parent's retained counters. New feature, occurrence and mate IDs are explicitly
mapped back into the owning document's counters during merge. New named sources
and renamed features/occurrences must not collide with outside parent source;
the user chooses an explicit unique name if a conflict occurs.

Child edits retain the immutable checkout manifest through `doc_apply`, prepared
candidate publication, undo/redo and save/load. Read-only dependencies cannot be
edited or removed. Editable/new occurrences remain descendants of the selected
group, its root parent is retained, and every child mate endpoint stays within
that group. Feature parameter removal uses the ordinary explicit `p_remove`
recipe; global named parameters cannot be deleted by this contract.

## Review and atomic integration

Save the edited child file before merging it. Read the owning source document's
revision and request a proposal, then apply it at that revision:

```json
{"action":"merge","path":"C:/design/module-work.ark",
 "expected_revision":13,"preview":true}
{"action":"merge","path":"C:/design/module-work.ark",
 "expected_revision":13}
```

The proposal returns `commands`, `id_map`, `changed`, `child_sha256`, source hash
and expected revision. It does not publish changes. Pass that observed child hash
as `expected_child_sha256` when applying a reviewed proposal; the Studio review
does this automatically. A child file changed during review rejects instead of
applying an unreviewed recipe. Integration requires the source revision
observed at checkout, its complete authored SHA-256 and the original scoped
baseline/ownership manifest derived again from that source. Audits, provider
usage and geometry caches are excluded from the source hash. A later source
edit, including an outside edit, rejects: there is no silent rebase. A successful
merge increases the owning revision once; replay of that old child rejects.

The owning session applies the complete recipe through the same geometry,
assembly, manufacturing-source and persistence validation as ordinary
`doc_apply`. Invalid geometry and constraints leave the source unchanged. The
existing **128-command atomic batch limit** applies. Outside authored rows remain
in the parent document; neither child audits nor its checkout manifest are
inserted into the parent. Parent drawings retain their existing association and
normal stale-source behavior; container work creates or refreshes no drawings.

## Explicit unsupported boundaries

- A definition also placed outside the group cannot be isolated as editable.
  Choose a group containing all its occurrences or explicitly author a separate
  definition.
- Outside features or outside named source consuming an editable definition
  reject. Expand the group or explicitly revise that dependency first.
- Cross-boundary mates, external occurrence references and outside mates that
  control a retained ancestor frame reject. No constraint or solid is dropped,
  and no constraint is silently frozen.
- Global Skeleton ownership cannot currently be checked out as a subset.
  Nested checkout and merge into another checkout reject.
- Changing retained feature type, input list, Boolean mode or history order is
  not a supported merge rewrite. New features can retain their explicit inputs.
  Removing retained placement/datum parameter keys is also unsupported. Clearing
  a datum's existing support uses an explicit `support:null` command. Removal of
  retained mate motion/limits is outside this merge scope and rejects; edit that
  constraint in the owning document explicitly.
- Imported source payload replacement cannot be carried through a container
  edit. Use the producer's supported import/reimport contract.
- Drawings, photo canvases and global Skeleton source cannot be introduced
  through container integration. Technical drawing work still requires an
  explicit user request and the separate drawing contract.

## Studio controls and verification scope

The **Design containers** tool group exposes create, inspect, checkout, checkout
and open another ARK, merge edited checkout and inspect isolated workspace.
Merge first presents the actual command recipe and stable ID mapping; Apply
uses atomic validation. Cancellation preserves source authoring. The second
Studio process opens the explicit child file with `--load`; it owns its document
and revision and must save before parent integration.

`design_container_tests` covers scoped feature/parameter/occurrence merge,
read-only shared globals and external inputs, actual subset recomputation,
sketch/datum/expression closure and explicit datum-support clearing, source hashes
and revisions, namespace conflicts,
ID mappings, feature parameter removal, producer-first pattern rebinding,
invalid-geometry rollback, external consumers/shared definitions,
cross-boundary mates, ancestor pose, binary save/load, existing-file protection,
manifest validity, undo and stale replay. Fixtures are synthetic and create no
technical drawings.

`--self-test-design-container-ui` drives actual gallery mouse clicks and modal
fields for creation, read-only inspection, binary checkout, independent child
editing, review/application, cancellation, existing-file preservation and stale
replay. Real explorer controls verify that a newly appended parent appears above
its existing child and that collapse/expand hides/restores that child while
preserving source and revision. Review screenshots target the actual multiline
command-review modal, and Cancel is tested there as well as on setup forms.
It records dialog and viewport captures and a local receipt. Separate
process launch is not automated in that control test. Local tests and controls
do not qualify general large-assembly scaling, distributed execution,
manufacturing, or cost/design improvement.

Local acceptance recorded on **2026-10-14**, Windows native build:
`design-containers-023b` passed in **0.13 s**, including explicit datum-support
clearing and geometry-failure rollback. `studio-design-container-ui` passed in
**4.35 s** on the final report-format check with actual mouse/modal controls,
command-review cancellation and
parent-first explorer collapse/expand. The 1440 × 900 checkout, actual command
review, source ownership, expanded group and collapsed group captures were
visually inspected: fields and controls were legible, review exposed the real
recipe and ID map, and the occurrence appeared below its parent group and
disappeared from the explorer when collapsed. A further compact run requested
1366 × 768; the application applies its existing 800-pixel minimum height, and
the resulting **1366 × 800** captures were also inspected. The command review
retained CRLF indentation and scrolling, the parent/child hierarchy remained
visible and that actual control fixture passed. Height 768 was not tested by
those clamped captures. These are synthetic local
acceptance results for B.04/D.03; those milestones remain partial.
