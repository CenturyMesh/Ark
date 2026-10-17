# Associative construction datums

Milestones: **B.01** (selection and visibility), **B.02** (editable references),
**B.03** (reference frames for exact sweeps). Implementation is scoped to named
construction references. It does not qualify sweep manufacture or general
spatial constraints.

New typed authoring uses conditional document version 34; existing simple
documents retain their applicable earlier format. This reflects a changed
authoring contract, independently of the application's public iteration code.

Construction definitions use existing `planes` records with a `kind` of
`plane`, `axis`, `point` or `frame`. Their `support` object persists the authored
expressions and source references. `plane.set` / `plane.remove` remain the
atomic authoring commands. Use the inspected `expected_revision` in `doc_apply`;
a stale revision, failed evaluation or missing dependent reference rejects the
whole batch. A source deletion does not silently turn a dependent datum into a
coordinate snapshot.

| Support kind | Required fields | Meaning |
| --- | --- | --- |
| `offset_plane` | `plane`, `offset` | Move along the source plane normal, in mm. |
| `angle_plane` | `plane`, `axis`, `angle` | Rotate the complete plane around an axis lying in that plane; angle in degrees. |
| `plane_three_points` | `a`, `b`, `c` | Origin at `a`, X along `a` to `b`, normal from ordered noncollinear points. |
| `plane_axis_point` | `axis`, `point` | Plane containing the axis and an external point. |
| `plane_point_normal` | `point`, `axis` | Plane through a point with normal along the axis. |
| `axis_points` | `start`, `end` | Axis from the first point toward the second. |
| `axis_planes` | `first`, `second` | Exact intersection line of two nonparallel planes. |
| `point` | `point` | Position driven by an explicit point or semantic source. |
| `between_points` | `start`, `end` | Frame with Z from start to end; optional `fraction` 0..1, `roll` in degrees, `x_axis` guide. |
| `bisector` | `first`, `second` | Plane through two plane anchors. Parallel anchors use the midplane and the first normal; `side:difference` flips that normal. Intersecting anchors use the angle bisector through the intersection (`sum` or `difference` of the unit normals). Optional `side` is `sum` or `difference`. A stored bisector saves document version 46. |

All scalar values accept finite numbers or parameter expressions. Offset and
rotation parameters `p:{ox,oy,oz,rx,ry,rz}` still compose locally after the
support recipe. Existing `face`, `midplane`, `normal_to_curve`, `tube_path`,
`on_curve` and `on_face` supports remain available.

Frames resolve to world coordinates using column-major matrices. Their local
X/Y span a plane and local Z is its normal or an axis direction. Explicit XYZ
point arrays are world coordinates. A `local` point anchor is transformed by
the referenced frame. Datum `p` offsets and rotations compose in the resolved
support frame; lengths are mm and authored angles are degrees.

Point anchors are:

- `[x,y,z]`, with numbers or expression strings;
- a datum name, or `{"datum":"PointName"}`, selecting that datum's origin;
- `{"datum":"Workplane","local":["u","v",0]}`, attaching a point to
  local plane coordinates that follow its position and orientation;
- `{"feature":42,"reference":"edge.0","endpoint":"end"}` (or `start`),
  using the producer's current exact semantic edge;
- `{"feature":42,"reference":"edge.0","fraction":"0.25"}`, sampling
  the exact reference by the existing arc-length contract;
- `{"sketch":"Path","entity":"Guide","endpoint":"end"}`, following
  an authored open line, wire, spline, arc, or point in its sketch plane.

Plane anchors are plane/frame names or `{"feature":42,"reference":"z_max"}`
for an exact planar face. Axis anchors are axis/frame names,
`{"datum":"XY","axis":"x"}` (also `y` or `z`), or exact edge/axis
references. An edge frame defines its local tangent line at the specified
fraction; it does not assert that the entire curved edge is straight.
Producer names supplied by the command host bind to stable feature IDs before
persistence, including nested references. Missing/ambiguous semantic names
reject. Sketch entity references require explicit unique names.

```json
{
  "expected_revision": 12,
  "commands": [
    {"op":"plane.set","name":"EndPoint","kind":"point",
     "support":{"kind":"point","point":{"sketch":"Path","entity":"Guide","endpoint":"end"}}},
    {"op":"plane.set","name":"SectionAtEnd","kind":"plane",
     "support":{"kind":"plane_point_normal","point":"EndPoint","axis":{"datum":"XY","axis":"z"}}},
    {"op":"plane.set","name":"StationFrame","kind":"frame",
     "support":{"kind":"between_points","start":[0,0,0],"end":"EndPoint","fraction":"0.5","roll":"15"}}
  ]
}
```

The between-points frame is dynamic under either endpoint edit. Its Z direction
follows the segment. For a controlled section orientation, provide `x_axis`;
the guide is projected perpendicular to Z. A parallel guide rejects instead of
changing tolerance or silently choosing another direction. Without a guide the
resolver chooses the least-aligned global axis deterministically. That choice
can change at alignment boundaries; use an explicit guide where orientation
continuity matters. This frame construction does not itself implement a
parallel-transport field along an arbitrary sweep.

New documents expose implicit Origin, XY, XZ and YZ. They are not stored in
`planes` and do not change the document version. Implicit Origin is a point at
world zero and is not an active sketch plane. A legacy authored datum named
Origin retains its authored kind, transform and dependents instead of being
rebound to world zero. Its tree row replaces the implicit Origin row; editing
is allowed, while deletion rejects if retained consumers still refer to it.
New creation under that reserved name rejects. XY, XZ (normal −Y) and YZ (normal +X) are the standard
planes. `plane_inspect` marks them `builtin` and `immutable`, with `role`
`starting_point` or `standard_plane`. Origin stays `kind` `point`.
`selection_set` with `plane` and `expected_revision` selects one of these names
or a stored datum and does not edit the document. Mixing `plane` with `id` or
`reference` rejects. Studio lists them under Reference geometry and can select
them when datums are shown.

Studio's construction tools create these typed definitions. Individual tree
visibility, selected datum, Show all and Hide all are view state. The viewport
uses plane quads, axis/frame line segments and point crosses for display and
selection; those display primitives are not production geometry. A selected
point/axis does not become the active sketch plane. Linked point creation can
use a plane origin, another point, or a curve endpoint.

Unknown fields, invalid scalar expressions, non-finite/composed out-of-range
frames, coincident axis points, collinear plane points, parallel intersection
planes, invalid rotation axes, and datum/sketch dependency cycles reject.
Closed curves have no endpoint anchors. Projection/offset/equation display
polylines are not endpoint authority; use a supported exact producer reference.

Validation record: `tests/datum_authoring_tests.cpp` covers analytical frame
coordinates and axes, parameter and source edits, live save/load associations,
missing references, malformed inputs, degeneracies, cross-resource cycles,
revision guards, candidate independence and session rollback. Native endpoint
coverage uses a line's producer-authored `edge.0` B-rep reference. Final native
test results and visual control/capture inspection belong in the iteration's
verification record; compiler syntax checks alone do not establish delivery.

Observed local core evidence: an isolated executable compiled with the current
datum/evaluation/serialization modules and the fresh core library passed **53
checks** on 2026-10-13. This includes restored live associations and nested
source-order rejection. OCCT endpoint checks were excluded from that core run;
native and visual acceptance require their separate test/capture results.

Evaluation scans nested anchors before cache reuse. A curve's `start_ref` or
`to_ref` may traverse point datums and sketch planes to a feature producer;
that producer must precede the consuming feature. Missing, suppressed, forward
and self/cyclic sources reject before native evaluation. Typed resource
consumers keep the ordered evaluation barrier. No source is recovered from a
previous recompute's body list.
