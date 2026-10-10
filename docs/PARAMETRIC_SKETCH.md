# Parametric sketch workspace (0.7)

The same `doc_apply` operations power the native editor and agents. Start with
`agent_onboard{"topic":"sketch"}` for a runnable example and the current revision.
The native document remains JSON; version 5 stores planes, sketch expressions
and splines while the reader retains versions 1 through 4.

## Construction planes

`plane.set{name,parent,p}` creates or edits a named coordinate frame. `parent`
defaults to `XY`; `p` accepts `ox`, `oy`, `oz`, `rx`, `ry`, `rz` expressions.
Offsets are in the parent frame, followed by local rotation `Rz * Ry * Rx`.
Lengths are millimetres and rotations are degrees. Built-in frames are:

| Plane | Local X | Local Y | Normal |
| --- | --- | --- | --- |
| XY | +X | +Y | +Z |
| XZ | +X | +Z | -Y |
| YZ | +Y | +Z | +X |

`plane_inspect{name}` returns its resolved column-major world matrix. Frames are
named datums, independent of tessellation face numbers. Cycles, missing parents
and deletion of a referenced plane fail atomically. Attaching a datum to an
arbitrary STEP face is not implemented.

## Coordinates and dimensions

`sketch.set{name,sketch}` accepts `plane`, `tolerance`, `entities`, `constraints`.
The default plane is XY and curve approximation tolerance is 0.05 mm.

Entity `dof` entries have two deliberate meanings:

- A **number** is a solver variable with that initial position/value.
- A **string expression** is a driven coordinate/value. It is evaluated before
  constraint solving and excluded from the solver's free variables, so another
  constraint cannot silently move it away from its parameter relationship.

For example, `{"kind":"circle","name":"mount","dof":["width-25","depth/2","4"]}`
defines an 8 mm diameter opening 25 mm from the right edge of a stock whose
local X extent is `[0,width]`. Changing `width` updates that relationship.

Alternatively use numeric entity coordinates and dimensions such as
`{"type":"fixed","a":0,"val":"width-25","val2":"depth/2"}` and
`{"type":"radius","a":0,"val":"4"}`. The existing coincidence, distance,
parallel, perpendicular, horizontal, vertical and angle constraints remain
available. Sketch angle constraints use radians; `deg(30)` converts 30 degrees.
An arc uses dof `[cx,cy,r,a0_deg,a1_deg]`. A wire is one entity with `start`
and `segments` of `line`, minor `arc` (`sweep` +1 or -1) and `cubic`.
Constraint `tangent` takes one line and one circle or arc. `sketch.patch`
changes one coordinate. A datum `kind` of `point` stores a location, and
support kind `on_curve` or `on_face` attaches a datum to an edge or a face.
Documents that use these save as version 12. The reader still accepts the
older versions described above.

A `project` entity drops an edge or a planar face of an earlier feature onto
the sketch plane. A parallel face becomes its boundary wire. A face that is
not parallel becomes the single segment where that boundary meets the plane.
An `offset` entity copies an earlier curve at a signed distance in
millimetres. Positive distance is to the left of the directed curve, the same
side as a wire sweep of +1, so a counter-clockwise loop offsets inward.
Both kinds are driven: the solver does not free their coordinates, and
`sketch.patch` rejects them. Replace the entity to change the distance.
These entities, a suppressed feature, and a plane canvas save as document
version 13.

`sketch_inspect{name}` reports solved coordinates, residual, local numerical
Jacobian rank, remaining degrees of freedom and profile readiness. The rank is
a local diagnosis, not proof of global uniqueness. Conflicting constraints
reject the edit; an underconstrained but converged sketch can be retained.

## Compact freeform curves

```json
{
  "name": "outline",
  "kind": "spline",
  "closed": true,
  "tension": 0,
  "dof": [
    "-width/2", "0", "-width/3", "-height/2",
    "width/3", "-height/2", "width/2", "0",
    "width/3", "height/2", "-width/3", "height/2"
  ]
}
```

These are **interpolation anchors**: the curve passes through them. They are not
Bezier control handles. Closed splines wrap automatically; do not repeat the
first point. Open splines accept 2–64 anchors; closed splines require at least 3.
The implementation uses centripetal Catmull–Rom converted to cubic Bezier
segments. `tension=0` uses the computed smooth tangents; larger tension shortens
them, and `1` gives zero endpoint tangents. There are no automatic shape-fit or
tangency claims for junctions between separately authored entities.

Adaptive de Casteljau subdivision bounds the distance from the curve to each
polyline chord using its Bezier control hull. Circle sampling uses the sagitta
bound and preserves quadrant extrema. Tolerance can be 0.0001–5 mm. Sampling
limits fail explicitly rather than loosening tolerance. This is an
approximation bound in the local sketch plane, not a physical fabrication
tolerance or a certification of the later mesh Boolean.

The agent can replace one curve without resending the entire sketch:

```json
{
  "op": "sketch.entity.set",
  "name": "profile",
  "entity": {
    "name": "outline",
    "kind": "spline",
    "closed": true,
    "dof": ["-width/2","0","0","-height/2","width/2","0","0","height/2"]
  }
}
```

`entity.name` is unique within a sketch. Existing entity positions/indexes stay
in place; replacing an entity that invalidates a constraint fails. Numeric
anchors can also be constrained using `pa`/`pb` as their zero-based anchor index.
`construction:true` excludes an entity from solid-profile extraction.

## Extrusions and cuts

`extrude` consumes `p:{sketch,height,both}`. Its direction is the sketch plane's
normal, and `both=1` centres it about that plane. `revolve` uses local Y as its
axis. A named sketch must have exactly one closed outer profile for either.

`sketch_cut` takes one base feature ID in `inputs`, and
`p:{sketch,depth,offset}`. Depth is positive along the cut sketch's normal; offset
is a signed displacement along that normal. A sketch can supply several
disjoint openings. For an extrusion from 0 to `thickness`, use `offset=-1` and
`depth=thickness+2` to cut through it. Blind cuts use the same explicit depth and
plane placement, not an inferred “through all” mode.

Lines and open splines can join at endpoints to form a profile. No closing edge
is invented. Open, branching, self-intersecting/touching sampled profiles and
nested loops are rejected. Nested loops must be expressed as separate cuts.
Imported source tubes remain read-only and are not edited by this operation.

Full-depth interior openings in an extrusion use a dedicated native prismatic
path: sampled polygons with holes are triangulated using vendored Mapbox Earcut,
then joined to walls with shared vertex indices. Every supplied boundary vertex
is retained. Cap area is checked against polygon area, indexed topology must be
closed/oriented, and volume must match net area times thickness within measured
floating-point rounding. Repeated cuts retain previous holes; rigidly moving the
part preserves this representation. Overlapping/touching/outside openings fail.

Other cuts use the existing general mesh Boolean and must pass closed oriented
mesh checks or the transaction is rejected. Its work budget is cooperative
(8-second deadline checks and a 1-million element limit), not a worker-level
hard timeout. Its geometric limitations are not repaired by a faster broadphase.

These are **sampled profile meshes**, not a new
exact B-rep kernel. General STEP export, fillets and persistent face references
remain outside this milestone. Existing source-tube STEP export is unchanged.
A sketch wire can still carry a parametric corner fillet or chamfer. That trim
is described below and is not a solid-edge fillet.

## Sketch fillets and chamfers

`sketch.fillet` `{name,entity,corner,radius}` and `sketch.chamfer`
`{name,entity,corner,distance}` trim one corner of a named wire inside
`doc_apply`. `corner` is the vertex index. On a closed wire the repeated
closing vertex is not another corner, and corner 0 uses the previous unique
point. The radius or distance stays an expression. The sharp wire remains the
solved geometry, so `sketch.patch` moving a vertex rebuilds the trim. A fillet
is the minor arc on the turning side: the center lies on the angle bisector,
and each adjacent straight edge shortens by `radius / tan(half-angle)`. A
chamfer cuts that same length, using the distance in place of the radius.
The saved record stores `entity`, `corner`, `kind` and `size`. Applying the
same entity and corner again updates the size and the kind. At most 64 corners
are stored. A fillet or chamfer saves as document version 14. Solid-edge
`native.op` fillet and chamfer remain a separate operation. The tool count
stays 34 and the product version stays 0.9.0.

## Evidence

`ark-sketch-tests` exercises tilted/nested planes, parameter changes preserving
hole edge distances, real through-hole ray intersections, volume against an
analytic holed plate, constraint conflicts, underconstraint diagnostics,
save/load and undo/redo. Spline approximation is compared with an independent
Barry–Goldman interpolator and tested under translation. The test also executes
the actual onboarding example and verifies a rotated symmetric extrusion.

The replayable example is `examples/freeform-plate.arkops`, with a saved
`examples/freeform-plate.ark` companion after generation. Agent operations can
be invoked through MCP or pasted into Studio's command editor.

`ark-prism-tests` independently checks area-times-thickness volume, through-hole
rays, repeated and batched openings, rotated/translated/antiparallel frames,
rejection boundaries, and the supplied organic demonstration. The example's
verified mesh has 596 vertices, 1200 triangles, one connected component, and no
boundary, non-manifold, inconsistent or degenerate elements. Its sampled volume
is approximately 30984.045739 mm³. These figures describe the shipped example,
not a claim of exact curved-solid or manufacturing certification.

The general CSG broadphase keeps the original candidate order, classification
and tolerances. `ark-csg-tests` checks analytic Boolean volumes and rays for
supported contained boxes plus work-limit rejection. Original-versus-optimized
organic probes kept identical geometry hashes; those probes also exposed the
legacy topology defects which motivated the dedicated prismatic path.

## Authored arc recipes — 005B / B.02

An `arc` can retain three source points instead of requiring the caller to
calculate its center, radius and angular interval:

```json
{"op":"sketch.entity.set","name":"Profile","entity":{"kind":"arc","name":"crown","recipe":{"type":"three_point","start":[0,0],"through":["width/2","rise"],"end":["width",0]}}}
```

The resolved arc passes through `start`, `through` and `end` in that order.
The through point selects the actual interval, including a clockwise interval
or an arc greater than 180 degrees. These are analytic circles; samples only
display the evaluated arc. Native extrusion, projection and surfacing consume
the existing analytic arc profile.

The tangent recipe takes two endpoints and an oriented start direction:

```json
{"op":"sketch.entity.set","name":"Profile","entity":{"kind":"arc","name":"transition","recipe":{"type":"tangent","start":[0,0],"end":[20,10],"tangent":[1,0]}}}
```

All recipe scalars accept finite numbers or parameter expressions. Numeric
recipe coordinates are authored values, and the derived center/radius/angles
are excluded from free solver coordinates. Parameters recompute the arc;
`sketch.entity.set` edits its compact source recipe. `sketch.patch` rejects
resolved coordinates of a recipe arc. A recipe and nonempty `dof` are exclusive;
the existing center/angle `dof:[cx,cy,r,a0_deg,a1_deg]` contract remains available.

`sketch_inspect{name,entity}` returns the source `recipe`, analytic length and
end tangents. Full inspection returns the recipe beside `resolved_dof`.
Persistence stores source inputs, not the cached center/angles, and requires
document version 30 only when the new semantics are used. Changing the
application iteration alone does not change document version.

Repeated or collinear three-point inputs, a zero tangent, or an endpoint on
its start tangent line fail atomically. The latter needs an explicit line;
there is no automatic substitution. A tangent recipe constructs a particular
arc from authored inputs. The existing planar `tangent` solver constraint
continues to constrain a line against a circle/arc's supporting circle; it
does not by itself join their endpoints.

`ark-sketch-arc-tests` covers source expressions, clockwise/major intervals,
parameter edits, source inspection, serialization/history/undo and rejection
without mutation. Its native configuration also checks analytic profile
extrusion and spatial arc sweep volumes. Test execution evidence belongs to
the iteration verification receipt; this capability does not qualify a design
for manufacture.

For an existing large design, inspect selected features with
`doc_info{detail:"design",ids:["Body"],sections:["features","sketches"]}`.
IDs select features; sketches, assembly and materials select their direct
associations. Explicitly requested parameters remain document-wide. The
response marks `partial`, `scope`, `section_scopes` and `total_counts`, while
keeping each returned row identical to the full inventory. Unknown or
ambiguous IDs and unknown sections reject. Omitting selectors retains the
full inventory contract. Smaller response bytes alone are not evidence of
lower design cost or higher design quality (D.01 remains a separate benchmark).
