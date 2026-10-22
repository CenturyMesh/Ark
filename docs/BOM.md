# Persistent part metadata and BOM — 011E

In 015B, the BOM workspace adds a dark layout, search, All/Needs review/Buy/Make
filters, status badges and selected-part details. Sort or filter before editing:
selection remains bound to the original definition ID. Locate part returns to
the model; Copy link copies an existing source URL without fetching it.

In 014D, **Manage → BOM** and **Inspect → BOM** expose the table, metadata and
sourcing editors, missing fields and local JSON export. `bom_review` adds a
revision-bound completion check after reading all BOM item pages. Detailed
scope rejects unresolved metadata; concept scope requires explicit pending
notes and a fastening review. The host labels incomplete concept completion.
See [014D](iterations/014D.md) for enforcement boundaries and evidence.

The `.ark` SQLite container retains part metadata, material assignments and
assembly occurrences. The BOM is derived from those definitions at the current
revision, so quantities are not a second independently editable list. JSON is
the versioned interchange format; it does not embed B-reps or display meshes.

## Authoring

Call `agent_onboard {"topic":"bom"}` or `{"topic":"fasteners"}`. Tool
discovery includes `bom_inspect`, `bom_review`, `bom_export` and
`fastener_recipe`. The ordinary `assembly_info.bom` contains the same enriched
items while retaining the earlier `feature`, `name` and `quantity` fields.

`bom.initialize` in revision-checked `doc_apply` assigns a host-generated
document identity. Setting `part` on `feature.add` or `feature.edit` also
initializes it. Library insertion does this in the same atomic transaction.
Reading an older document does not migrate it or invent metadata.

```json
{
  "expected_revision": 4,
  "commands": [{
    "op": "feature.edit",
    "id": "Mount bracket",
    "part": {
      "part_number": "LOCAL-BRACKET-001",
      "description": "Motor mounting bracket",
      "part_revision": "A",
      "category": "bracket",
      "procurement": "make",
      "representation": "exact"
    }
  }]
}
```

`part` replaces the complete metadata object; `null` clears it. Its fields are
`part_number`, `description`, `part_revision`, `category`, `procurement`,
`manufacturer`, `manufacturer_part_number`, `supplier`, `supplier_part_number`,
`source_url`, `datasheet_url`, `standard`, `specification`, `representation` and
`sourcing_status`. Unknown fields reject. Strings are bounded; links require
HTTPS without credentials. The existing feature `material` field refers to a
named document material. `manufacturing.route` remains the fabrication intent.
Metadata-only changes preserve native geometry and are part of undo/redo.

For an incremental edit, prefer `feature.edit` with `part_patch`. For example,
`{"op":"feature.edit","id":12,"part_patch":{"description":"Motor bracket"}}`
changes only the description. Omitted fields, including supplier references,
are preserved. The merged record must satisfy the same validation as `part`;
selecting sourcing may reuse its existing URL/reference, but clearing a required
source on a selected record rejects atomically. `part_patch` requires the current
`expected_revision`, is supported by preview/commit, and cannot be combined with
`part` or used on another operation. It changes no document format.

## Recoverable agent review — A.02 / D.02

A completion rejection reports current counts, bounded missing-identity rows,
unresolved field counts, document issues and the next inspection request.
Progress includes current authoring, revision and the inspected definitions.
Reading another page at the same revision is progress. This data is diagnostic,
not a completed inspection or a review attestation. Internal identity fields can
be assigned from design intent; unknown materials and commercial data remain
unresolved. Material is a feature field and fabrication intent is
`manufacturing.route`, not a key inside `part_patch`.

Single offers the BOM inspection/review tools after a completion rejection.
An explicit leading `needs_input:` request reaches the user without approving
project completion. Repeating a final answer at the same unresolved review state
ends with **BOM review pending**, retaining the work and withholding the proposed
completion text. This is distinct from a provider interruption. Team receives
the same structured recovery with its rejected `team_finish_goal` call.

For explicitly limited work, `bom_review{completion:"task",...}` may omit `ids`:
the host selects all affected definitions (up to 64). An empty affected set can
review display-only or read-only work after a current inspection. Explicit IDs
still must cover every affected definition. Removed definitions, unavailable
baseline or a larger set require project review. Concept/detailed requirements
are unchanged. No task receipt approves the global BOM.

A bare Resume/Continue request in an existing Single conversation preserves its
goal and BOM baseline. It continues the unfinished work rather than starting a
summary-only task. An explicitly requested summary remains a separate request.
The visible completion qualification is short; detailed pending/fastening notes
remain in the `bom_review` receipt. Receipts are session-local.

`procurement` is `make`, `buy` or `unspecified`. `sourcing_status` is
`unresolved`, `candidate` or `selected`; selection requires a source URL and an
explicit manufacturer/supplier reference. These are caller declarations, not
proof of stock, supplier identity, standards compliance or a purchasing order.
Internal part numbers are distinct from manufacturer references and external allocations.

New BOM identity/metadata requires document version **32**. Documents without
that data keep their applicable earlier version. Older readers reject v32
explicitly; their runtime identity alone does not change a drawing's format.

## Quantities, identity and missing information

With an assembly, counts come from resolved occurrences and pattern copies,
including hidden instances. Without one, the inventory uses terminal outputs;
consumed construction solids and curve features are not extra items. Groups,
parent nodes, copies and occurrence IDs preserve hierarchy separately from the
aggregate item quantities. Unplaced annotated definitions are reported.

Inspection defaults to 64 items and 128 occurrences. Use `offset`/`limit`
(maximum 100) and `occurrence_offset`/`occurrence_limit` (maximum 256) to read
further pages. `partial` and `page.total_*` are explicit; aggregate completeness
and `content_sha256` describe the whole snapshot. Match revision and authoring
hash across pages. Export always contains all items and occurrences.

Part and occurrence IDs are scoped by the persisted document identity and the
feature/node IDs. Renaming, metadata edits and edits to the same definition
preserve identity. Replacing a definition with a new final feature creates a
new identity: metadata is not silently inferred through fillets, booleans or
other geometry changes. An annotation left on a consumed input is reported for
explicit review/transfer. Cross-feature identity rebinding and cross-product
identity reconciliation are not implemented.

`bom_inspect` returns `ark.bom.v1`, document identity, session `revision`, an
`authoring_sha256` of persistent authoring without usage/history, items,
occurrences, groups, source custody and missing/review fields. Use both identity
and the authoring hash when comparing snapshots across sessions; the session
revision is not a globally persistent revision counter. Imported library
metadata is a retained source snapshot; updating the library does not silently
rewrite existing drawings. Source paths and geometry payloads are not exported.
`brep_base64_sha256` hashes the evaluated B-rep's base64 representation, not a
claim of canonical geometric equivalence.

Missing descriptions, materials, part numbers, purchase references and sourcing
selection remain explicit. Duplicate internal part numbers on distinct
definitions are flagged and never merge rows automatically. Simplified/envelope
geometry requires engineering review. `metadata_complete` is only a data
check; `manufacturing_qualified` is always false here, and fastener completeness
is not inferred from a nonzero count.

## Fixations and catalogue reuse

For specified purchased components, search the real library first, inspect the
selected record and preserve its original source and links. The BOM carries
its component ID, original STEP/B-rep digests, selected body, manufacturer
reference, source link, provenance and license declaration.

`fastener_recipe` is a separate read-only authoring aid. It returns three
ordinary native-solid commands with the current `expected_revision`:

- Screw: explicit `diameter_mm`, under-head `length_mm`,
  `head_diameter_mm` and `head_height_mm`; coaxial cylindrical shank/head.
- Washer: explicit `inner_diameter_mm`, `outer_diameter_mm`, `thickness_mm`.
- Nut envelope: the same annular dimensions; cylindrical, without hex faces.

All require a unique `name`; optional `part` and named `material` carry declared
metadata. Dimensions are recorded as caller-declared nominal values at recipe
creation; if geometry is subsequently changed, review that specification.
No modeled threads, drives, tolerances, standards recognition or supplier
selection are implied. The recipe does not add catalogue records. Apply it
atomically, inspect native validity and reuse the final definition through
assembly placements. A detailed design still needs interface, clearance and
fastening review; an open concept may explicitly retain those gaps.

## Export and external integration boundary

`bom_export {"expected_revision":N,"path":".../bom-rN.json"}` writes a new
local snapshot and reports its SHA-256. Identity must be initialized. Drafts may
be exported with their missing fields intact. Publication is atomic and never
overwrites an existing destination; filesystems without hard links reject.
The export contains private design metadata and is never uploaded implicitly.
The complete-export JSON Schema is [ark-bom-v1.schema.json](schemas/ark-bom-v1.schema.json).
The Windows package also carries this contract and guide in `assets/contracts`.

The export reports no implemented external adapter.
Organization authorization, remote identity mapping, incremental import,
idempotency and preservation of release/procurement work require a separate
integration contract and end-to-end tests. This local foundation addresses
B.05/E.01 and supports future E.03; it does not declare that integration done.
