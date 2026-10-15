# Explicit surface orientation repair — 029B

Roadmap **B.03: Parcial**. The local scope adds an editable, explicitly requested
orientation operation. It does not close geometric gaps, rebuild missing faces,
fit replacement surfaces, heal arbitrary imported geometry or qualify manufacture.

## Authored contract

Use `doc_apply` with the current `expected_revision` and one exact feature input:

```json
{
  "op": "feature.add",
  "name": "Oriented surface",
  "type": "surface_edit",
  "inputs": ["Original underside"],
  "native": {"op": "repair", "mode": "orientation", "normal": [0, 0, "N"]},
  "p": {}
}
```

`normal` is a source-local expression vector and is required for a single planar
face. It must be parallel to that face's exact support normal. Its sign chooses
the desired orientation; it never rotates or replaces the support. A curved or
periodic face cannot accept a single global normal. Changing `N` from `1` to `-1`
recomputes the face and subsequent signed thickening. The producer feature and
its authored expressions remain in the history. A changed producer recomputes
the repair through the ordinary feature dependency graph.

Without `normal`, the supported input is one connected shell or one single-shell
solid. The operation makes neighbouring face orientations consistent and makes
a closed solid outward. Open shell boundaries remain open. Multiple solids,
compounds, cavities, disconnected shells, redundant faces and nonmanifold edge
uses reject; no subshape is silently dropped. Limits are 512 faces and 4096 edges.

Persistence selects source format **41** only for documents containing this
recipe or another format-41 contract. A repair recipe relabelled as version 40
rejects during loading. Existing older documents retain their format selection.

## Native evidence and custody

The worker operation is `surface_repair` with `mode:"orientation"`, `input_brep`
and optional source `topology` and `normal`. This explicit operation alone parses
an invalid candidate before checking it. Ordinary B-rep loading and authoring
retain their validation gates. Invalid neighbour orientation can therefore be
tested at the native boundary without making arbitrary invalid imports available
through ordinary document evaluation.

The kernel uses only `ShapeFix_Shell::FixFaceOrientation`, followed by
`BRepLib::OrientClosedSolid` for solids. It does not call general shape/face
healing, sewing, tolerance adjustment or mesh reconstruction. Every individual
input face must pass OCCT geometric validation first. The output must pass
`BRepCheck_Analyzer` with its exact method enabled and the separate
`BRepCheck_Shell::Orientation` check. OCCT's exact curve-on-surface method applies
to SameParameter edges; this is not a global self-intersection proof.

`native_inspect.report` contains:

- `before` and `after`: validity, orientation consistency, unique solid/shell/
  face/wire/edge/vertex counts, connected components, seam and degenerate-edge
  counts, free boundaries and lengths, area, maximum entity tolerance and, for
  solids, signed volume.
- `changed_faces` and `changed`: changed face orientations. These are not claims
  that serialized output bytes are unchanged when no orientation changes.
- `geometry_deviation_mm:0`, backed by matching each native face by identity and
  comparing its complete serialized forward-oriented representation before and
  after. Supports, pcurves, boundaries and entity tolerances remain identical.
- `source_brep_sha256`, `tolerances_preserved`, `all_faces_preserved`,
  `all_solids_preserved`, and explicit `gap_healing:false`.

Semantic face, edge and axis aliases map through exact native identity. A planar
face's descriptor normal is recomputed. Current-B-rep handles retain their
original digest and become obsolete when that digest changes; fresh handles are
published for the result. Stale source topology rejects explicitly.

Rejections retain stable `repair_*` reasons, including `repair_invalid_request`,
`repair_unsupported_mode`, `repair_unsupported_shape`, `repair_disconnected`,
`repair_nonmanifold`, `repair_open_solid`, `repair_invalid_geometry`,
`repair_normal_required`, `repair_unsupported_normal`, `repair_normal_mismatch`,
`repair_stale_topology`, `repair_invalid_reference`, `repair_unorientable`,
`repair_custody_violation`, `repair_invalid_result`, `repair_invalid_input` and
`repair_kernel_failure`. Document transactions retain the revision, source,
history and previous evaluated geometry on rejection.

## Synthetic verification corpus

`tests/occt_surface_repair_tests.cpp` constructs synthetic native candidates,
including an inconsistent six-face shell, an invalid solid, inward periodic
cylinder, degenerate cone apex, eccentric loft, sweep and thickened face. Negative
cases exercise missing boundary edges, an open solid, disconnected and redundant
faces, multiple solids, stale aliases, curved-face normals, tilted normals and
tolerance widening. No user documents or reconstructed meshes are fixtures.

`tests/corpus/surface-repair-029b.json` adds editable document transactions at
scales 0.5, 1 and 2. It checks actual downstream thickening bounds on both sides
of the repaired face, producer edits, periodic seam preservation, source-41
roundtrip/recompute, downgrade rejection and atomic failures. The standalone
`surface-repair-manifest-029b.json` pins its exact bytes.

The prior authoring corpus and surfacing tests remain required companions;
orientation repair does not replace their loft, sweep, trim, sew or continuity
coverage. Receipts record failed attempts as well as the final accepted run.
This evidence is local native/document verification, not interactive GUI,
cross-product interoperability, deployment or manufacturing qualification.

Observed local results on 2026-10-15:

| Suite | Observed result |
| --- | --- |
| Explicit native repair | 21 trials, 0 failures |
| Editable repair corpus, three scales | 36 trials: 18 accepted, 18 expected atomic rejections, 0 failures |
| Existing native authoring corpus | 84 trials: 57 accepted, 27 expected atomic rejections, 0 failures |
| Existing native surfacing suite | Passed: loft/STEP, fill, NURBS/thicken, sweep/extrude/revolve/offset/sew, loft-tip and rejection groups |

The [kernel receipt](verification/029B/surface-repair-kernel.json) and
[editable authoring receipt](verification/029B/surface-repair-authoring.json)
contain measured geometry and transaction results. The
[attempt history](verification/029B/surface-repair-attempts.json) retains the
earlier failed checks and their corrections: shell orientation requires its own
check beyond generic validity; explicit repair needs a local candidate reader
for typed invalid-input diagnosis; format 41 must inspect the SurfaceEdit native
operation and be admitted by the iteration-history loader. Those attempts were
not counted as passing trials or omitted from the evidence.
