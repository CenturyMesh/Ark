# Editable 3D cables and flat ribbons — 035B

035B extends **A.01/A.03/B.03/B.04/D.02** within the explicit local scope below.
These milestones remain **Parcial** outside that scope. This contract creates
editable design geometry; electrical, connector-fit and manufacturing
qualification remain separate work.

## Studio path guide

Electronics offers **Smooth cable**, **Ribbon cable** and **Guide path**. The
guide displays a selected authored cable's start, waypoints and end as draggable
controls. Each gesture uses the current view plane; orbit between gestures to
change that plane. An immediate OpenGL control cage follows pointer movement.
The dedicated immutable preview worker coalesces pending requests and returns
the native sweep for the latest generation. Pointer movement does not publish a
CAD edit. Releasing applies one prepared current candidate and creates one Undo
step. Escape, capture loss or a stale document revision cancels the gesture.

An occurrence-bound point is resolved once when the guide arms. Dragging edits
its semantic-frame `offset` while retaining node/copy/reference and connector
labels. Routes with `auto_route:true` need an explicit conversion to the authored
spline mode before guide editing; choosing spline/ribbon in the form performs
that requested conversion and turns automatic routing off.

`ark-studio --self-test-cable-guide-ui` exercises rapid pointer updates, no CAD
write before release, request coalescing, one-step Undo, cancellation and stale
revision rejection. Timing evidence separates UI handler p95/p99 from native
preview time including all geometry gates. A queued request or a moving cage
does not establish that native geometry has finished. Execution results belong
to the iteration verification receipt.

## Authored routes

`cable_route` retains its existing feature type and revision-bound
`doc_preview` / `doc_apply` workflow. An absent `route_mode` retains the earlier
rounded-polyline algorithm, auto-route rules and source-format needs. Existing
saved recipes are not reinterpreted or migrated by the runtime identity.

An explicit `route_mode:"spline"` interpolates `start`, ordered `waypoints`, and
`end` with a native OCCT nonrational spatial BSpline:

```json
{
  "type":"cable_route",
  "name":"Controller lead",
  "native":{
    "route_mode":"spline",
    "start":{"name":"controller","point":[0,0,0],"connector":"J1","pin":"1","net":"power"},
    "waypoints":[[30,8,6],[60,-5,12]],
    "end":{"name":"motor","point":[90,0,15],"connector":"J2","pin":"A","net":"power"},
    "radius":0.5,
    "bend_radius":3
  }
}
```

The receipt's actual `centerline.degree` and `centerline.multiplicities` determine
continuity: an interior knot of multiplicity m in a degree-p spline has C(p−m)
parametric continuity. OCCT's interpolation can use lower degree for a straight
route, and tangent constraints can lower continuity to C1. C2 is not a global
promise for every authored recipe. Optional paired `start_tangent` and
`end_tangent` three-vectors constrain the forward route direction at the
endpoints. Their magnitudes are rescaled by the interpolation
algorithm, rather than representing a physical force or tangent-handle length.
The vectors are authored in the same world coordinate system as the route.
Changing an occurrence-bound endpoint moves the point and does not rotate these
independent authored vectors. Omit both constraints when that direction should
follow the interpolated route.

There are 2..34 anchors: index 0 is `start`, indices 1..N are the N authored
`waypoints`, and index N+1 is `end`. There are at most 32 waypoints. Inserting or
removing a waypoint changes later positional indices; the retained endpoint
names and occurrence IDs continue to identify their original sources. An editor
must preserve the enclosing point object when editing `point` or bound `offset`.
A bound point remains a binding; replacing it with an unrelated world point is
an explicit source edit. During an interactive preview, a control cage or the
last published mesh is display evidence. A successful current native worker
receipt is required to publish changed geometry.

Spline mode accepts an authored route, not `auto_route:true`. Obstacles never
silently alter its path. Anchors must be finite, within ±10000 mm on every axis,
and pairwise separated by at least 0.0001 mm. The native control hull must also
stay in that coordinate domain. Curve degree is at most 3, with at most 102
poles and 33 Bezier spans. The integrated route must be at most 100000 mm long.

## Rectangular ribbon section and transport

Omit `profile` and use `radius` for a round spline. A ribbon instead authors:

```json
{
  "route_mode":"spline",
  "start":[0,0,0],
  "waypoints":[[30,8,6],[60,-5,12]],
  "end":[90,0,15],
  "profile":{"type":"ribbon","width":4,"thickness":0.4,"up":[0,0,1]},
  "bend_radius":8
}
```

`up` is the initial section's thickness direction. Its perpendicular component
to the actual initial spline tangent defines the orientation; a zero or nearly
parallel vector rejects (normalized perpendicular magnitude below 0.000001).
Width follows `up × tangent`. OCCT corrected-Frenet transport
carries this initial section through the route, including nonplanar paths and
inflections. No arbitrary Frenet frame flip or discrete polyline section is
used to choose production geometry. A separate authored twist law is outside
this contract.

Width is in [0.0001,1000] mm; thickness is in [0.0001,100] mm and must not exceed
width. Round radius is in [0.0001,100] mm. `radius` and a ribbon profile together
reject. `p.width` / `p.thickness` override the ribbon's dimension expressions;
`p.radius` / `p.bend_radius` retain their existing expression role.

`bend_radius` is the required **centerline** minimum radius, not a corner-rounding
parameter in spline mode. It must exceed the section's circumradius
(`radius`, or `sqrt(width²+thickness²)/2`) and be at most 100000 mm. Its default
is three times that circumradius. This conservative section extent avoids a
locally folded swept section; it does not establish a real cable's allowable
strain or a flat flex manufacturer's directional bend specification.

The worker builds a native NURBS B-rep sweep at a fixed 0.000001 mm surface
tolerance and reports OCCT's approximation error. This is native exact
representation authority with a measured sweep approximation, not a claim of
symbolic equality for every procedural sweep surface. The renderer's triangles
remain display data.

## Rejection, receipts and attachments

All writes retain `expected_revision`, dependency checks, scratch evaluation and
atomic publication. Failed interpolation, curvature proof, frame, B-rep validity,
self-interference or obstacle checks reject the complete candidate. Typed error
prefixes include `CABLE_PATH_DEGENERATE`, `CABLE_FRAME_INVALID`,
`CABLE_BEND_TOO_SHARP`, `CABLE_CURVATURE_UNPROVEN`, `CABLE_SELF_INTERSECTION`,
`CABLE_OBSTACLE_COLLISION`, and `CABLE_SWEEP_UNPROVEN`. There is no alternate
geometry, simplified path, broadened tolerance or partially accepted prefix.

The portable normalizer validates the authored domain and reports
`route_chord_length_mm`. It leaves native length, curvature and obstacle evidence
pending. The worker's `electronics_inspect` report adds:

- Native integrated `route_length_mm`, full nonrational `centerline` BSpline
  poles/knots/multiplicities, original ordered `path` anchors and cross-section
  dimensions/area.
- `start_frame` and `end_frame` measured from the actual native terminal
  sections, with origin, tangent, width direction and thickness direction.
- A conservative numerical curvature bound derived from nonrational Bezier
  derivative convex hulls, with fixed floating-point guards, subdivision count
  and observed sample curvature. Samples alone never establish acceptance.
  Unresolved regularity or radius bounds reject after depth 14 or 65536 visits;
  more than 512 accepted injective leaves also reject before native inspection.
- OCCT self-interference checks on the centerline's injective Bezier-leaf wire and swept
  B-rep. Each leaf additionally bounds total turning below π/4. The actual sweep
  preserves those same exact de Casteljau boundaries as native topology so
  different lateral faces expose thick-profile near-overlap to BOP, even when
  the centerline itself does not cross. The subdivision changes neither the
  authored interpolation nor the reported BSpline. Native B-rep validity and a
  single positive-volume solid are required. A terminal round section returned
  as a BSpline is checked against its full rational Bezier implicit circle and
  plane bounds at the same fixed tolerance, rather than fitted from samples.
- Native swept-solid distance to each declared AABB, rejecting touch/overlap
  within the fixed sweep tolerance. `obstacle_count:0` gives no environment
  collision evidence. The minimum clearance is present only for a declared
  nonempty obstacle set.

The existing occurrence-aware `{node,copy?,reference?,offset?,name?}` contract
remains available for start/end/waypoints. Producers remain explicit `inputs`;
names do not replace stable node IDs. Start and end can additionally retain
nonempty `connector`, `pin` and `net` strings of at most 128 bytes. Resolution
preserves those labels in the normalized endpoint and occurrence-binding
receipt, including sampled assembly poses. The labels do not create connector
bodies, electrically join nets, verify pin compatibility or establish fit.
Occurrence-derived obstacles remain conservative world AABBs as documented in
[031B sampled routing](ASSEMBLY_SWEEP_031B.md).

Explicit new route/profile/tangent/endpoint-label semantics conditionally require
source45. Unchanged legacy cable and PCB recipes keep their previous document
format. Runtime release identity does not change the `.ark` container version.

`manufacturing_qualified:false` and `dynamic_physics:false` remain explicit.
Clearance is static against declared boxes. Material stiffness, strain,
allowable directional flex radius, twist, clips, continuous motion, fatigue,
electrical continuity and thermal behavior require separate evidence.

## Verification scope

`ark-electronics-tests` extends the existing portable/core and native test
branches with synthetic authored recipes. It covers old source16 round trips,
conditional source45 and downgrade rejection, bounded inputs and metadata,
nonplanar round/ribbon geometry, native length and cross-section/volume
agreement, endpoint orientation, vertical/straight routes, too-sharp and
self-crossing rejection, full-profile obstacle collisions, atomic failure,
occurrence-label persistence and sampled slider recomputation.

Test execution status is recorded in the iteration verification receipt. These
fixtures contain no user drawings and establish no physical fabrication or
electrical qualification.

On 2026-10-17, the fresh native and portable `ark-electronics-tests` executables
both passed. The native focused CTest set passed 14/14, including `electronics`
in 1.63 s. The native branch covers the actual binary `.ark` round trip,
revision-conflict and geometry-failure atomicity, labels on occurrence-bound
points, and all three sampled slider poses. The portable branch validates the
numeric contract and compatibility without claiming native geometry checks.

The spatial example above measured length 98.6226797031006 mm in both profiles.
Round volume was 77.4580712496712 mm³; ribbon volume was
157.796286998401 mm³. Their relative differences from authored section area
times native length were approximately 3.34e-9. The bounded radius was
20.1647052065164 mm across seven accepted exact Bezier leaves. Native surface
errors were 4.68e-7 mm and 6.85e-7 mm respectively, below the fixed 1e-6 mm
acceptance threshold.

Both spatial receipts have degree 3 and knot multiplicities `[4,1,1,4]`, giving
C2 at the two internal knots. The two-point vertical example has degree 1 and
multiplicities `[2,2]`. The straight example with paired endpoint tangents has
degree 3 and multiplicities `[4,4]`. Those last two curves have no internal knot
at which to claim an interior join continuity order.

The skewed near-approach fixture uses anchors
`[[-600,-300,0],[600,300,1],[-600,300,2],[600,-300,3]]`, round radius 2 mm and
required bend radius 3 mm. Its native centerline inspection passes, then the
actual thick swept profile rejects with `CABLE_SELF_INTERSECTION: swept profile`.
The planar crossing fixture rejects at centerline inspection. A straight ribbon
also rejects an obstacle touching only its broad edge, beyond the centerline.

Initial verification found an order-sensitive JSON test assertion and an exact
floating-point-zero assertion; both were corrected without changing geometry
or acceptance limits. It also found that a valid round terminal profile could
be returned as a BSpline. The final implementation verifies that complete native
curve against circle/plane bounds rather than requiring an analytic curve tag.
The stronger self-interference check preserves exact leaf topology through the
sweep. Preliminary results preceding those corrections are not acceptance
evidence. Isolated interactive latency, manufacturing and electrical validation
are outside these local geometry fixtures.

The interpolation and transport follow the primary OCCT APIs:
[GeomAPI_Interpolate](https://dev.opencascade.org/doc/refman/html/class_geom_a_p_i___interpolate.html),
[Bezier decomposition](https://dev.opencascade.org/doc/refman/html/class_geom_convert___b_spline_curve_to_bezier_curve.html)
and [BRepOffsetAPI_MakePipeShell](https://dev.opencascade.org/doc/refman/html/class_b_rep_offset_a_p_i___make_pipe_shell.html).
