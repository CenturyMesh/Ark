# Exact split lines — 024B

024B adds a bounded editable face-partition operation under B.02/B.03. A split
line projects one connected sketch wire onto one explicitly selected face of an
exact solid and creates separately selectable face regions on that same solid.
The original solid remains a retained upstream feature. The operation neither
cuts out material nor creates separate physical parts. Colors and textures can
then bind to the produced exact face references through face appearance recipes.

## Authoring contract

Inspect the source using `native_inspect{id}` and retain its exact face name and
`topology.brep_sha256`. Create the sketch on a saved plane using the existing
`plane.set` and `sketch.set` contracts. A closed circle or connected closed wire
creates an enclosed region. An open curve must meet the selected face boundary
at both ends after projection. Curves extending beyond the face are clipped by
the exact projection onto the selected bounded face.

```json
{
  "expected_revision": 12,
  "commands": [{
    "op": "feature.add",
    "type": "native_solid",
    "name": "Face regions",
    "inputs": [1],
    "native": {
      "op": "split_line",
      "sketch": "Region border",
      "source_face": "z_max",
      "source_topology": "<observed topology.brep_sha256>",
      "direction": [0, 0, -1]
    }
  }]
}
```

`native.direction` accepts three numeric values or expressions in source-local
coordinates. The sketch resolves its saved construction-plane frame in those
same coordinates. Direction is mandatory, nonzero, and must not be parallel to
the sketch plane. The operation chooses forward projection only. The target
face is explicitly selected; it does not choose another face hidden behind or
in front of that target.

The accepted target domain is an exact planar or cylindrical face. A cylinder
can produce front and rear projection branches. The nearest forward branch is
accepted only if exact curve bounds prove that its depth interval precedes the
other intervals across the whole connected wire. Overlapping intervals and
disconnected projected curves reject explicitly. No sampled point or mesh
chooses the branch. One feature consumes one connected wire and one face;
additional borders can be separate downstream split features.

## Exact identity and output

The original face reference becomes `split` and publishes `split_into`. The
output has selectable producer references:

- `split.<source digest first 12 characters>.region.N` for each face region;
- `split.<source digest first 12 characters>.boundary.N` for shared exact edges.

`native_inspect` exposes these references and `report.split_line.regions`, with
region areas and centers. Region numbering follows ordered exact area centers;
it is not a user-authored semantic label for an inside or outside region. A
sketch edit can change that order. Appearance recipes must retain the observed
result topology digest and be reviewed/rebound when the geometry changes.

`report.split_line` retains the exact projected wire B-rep, source face/digest,
projection mode and branch count, region references, shared boundary references,
source volume, volume delta, symmetric material difference and boundary area
delta. This is derived inspection evidence; the saved source retains the
editable sketch and split recipe rather than a baked projection result.

When the sketch and target planes are parallel, projection is one exact rigid
translation followed by exact intersection with the bounded face. A sketch
already on that face needs no artificial offset. This preserves the source
analytic/rational curves, including for oblique directions. Other accepted
cases use OCCT directional surface projection, still subject to the fixed
entity tolerance checks. `projection_representation` identifies that choice.

The implementation uses the source exact wire and local B-rep face splitting.
It verifies a single positive-volume valid solid,
the expected face census, unchanged exact support surfaces, conserved region
areas, conserved total boundary area and volume, bounded symmetric material
difference, unchanged bounds, and entity tolerances at the fixed **1e-6 mm**
limit. It does not increase tolerances or substitute a display mesh on failure.
Surface-area verification uses adaptive exact integration at fixed relative
precision `1e-12`; curved trim areas are not accepted using the coarser default
nonadaptive integration.
Unsupported surfaces, no intersection, backward-only intersection, a dangling
interior curve, disconnected wires, stale exact source identity and invalid
partitions reject the atomic document edit.

Split recipes conditionally require document format **39**. The 024B release
identity alone does not change a document's format. Sketch dimensions remain
editable and drive recomputation. A change to the retained source body digest
rejects until the caller explicitly selects and binds the current exact source.
Writes still require the observed `expected_revision`; undo and reload preserve
the source recipe. Face partition does not establish manufacturing qualification
or complete B.02/B.03 beyond this bounded local scope.

## Verification scope

The local `native-split-lines-024b` target passed **167 checks** after the 024B
native build. It exercises planar circle and rectangle partitions,
coplanar/oblique projections, boundary-to-boundary open lines, cylinder branch
selection and curved support preservation, region references,
one-solid/material/area conservation, exact STEP roundtrip, stale and invalid
rejection, sketch parameter recomputation, source persistence, version gates and
undo. This proves the stated local domain. General curved/freeform surfaces,
multiple disconnected sketch wires and manufacture remain outside that evidence.

## Plane sections

A viewing section is separate from a face split. Existing exact persistent
section curves use a retained body plus a saved plane:

```json
{"op":"feature.add","type":"intersection_curve","name":"Section contour","inputs":[1],"native":{"plane":"Section plane"}}
```

That domain requires one connected exact wire with at most 128 edges. Read-only
`geometry_query` with `action:"section"`, an explicit origin and normal reports
each section loop's length and enclosed area. Multiple nested loops remain
separate in the report; their areas are not automatically combined. A visual
section does not modify the exact body or create an exportable cut solid.
