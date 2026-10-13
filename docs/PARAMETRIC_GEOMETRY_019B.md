# Editable native geometry in 019B

This work addresses B.02, B.03 and D.04 within a bounded local scope. Those
milestones remain **Parcial**. The native operations use OCCT B-reps; previews
and picking meshes do not choose production geometry. All authoring commands
retain `expected_revision` and atomic publication. A rejected source edit or
derived operation preserves the prior document and revision.

## Helix source

Create an ordinary editable `curve` feature:

```json
{"op":"feature.add","type":"curve","name":"Coil","native":{"op":"helix","radius":"20","pitch":"10","turns":"2.25","start_angle":"30","origin":[0,0,0],"axis":[0,0,1],"x_axis":[1,0,0],"handedness":1}}
```

Scalar and XYZ coordinates accept document expressions. Radius and positive
axial pitch must exceed `1e-6` mm; turns are in `(1e-6,100]` and total rise is at
most `1e7` mm. `start_angle` is in degrees. The nonzero radial `x_axis` must be
perpendicular to `axis`; the default picks a perpendicular world axis.
`handedness:1` rotates toward positive local Y and `-1` toward negative local Y,
while both advance in the positive authored axis direction. Unknown fields,
spatial constraint handles and feature inputs reject.

OCCT 8's native helix builder generates connected B-spline coils with a fixed
requested `1e-7` mm fit tolerance. ARK rejects kernel warnings or a tolerance
failure, and compares 65 checkpoints per coil to an independent analytic
formula. The report exposes requested/reached tolerance, observed checkpoint
error, analytic length and measured native length. The serialized B-rep is a
native B-spline representation; it is not labelled an exact analytic helix or
a proof between all checkpoints. The editable analytic source is retained.

A helix name can be `sweep.native.path`, an open loft section, or a guide when
the guide construction and its section contacts satisfy the supported loft
domain. Sweeps still require a geometrically suitable positioned section.
Self-intersections, arbitrary spring contact and fabrication qualification are
not inferred from successful construction.

## Sewing surfaces

```json
{"op":"feature.add","type":"sew","name":"Joined skin","inputs":["Skin A","Skin B"],"p":{"solid":0},"native":{"require_connected":true}}
```

Sew accepts 2..16 exact faces or shells and retains the fixed `1e-6` mm modeling
tolerance. `require_connected:true` rejects disconnected surface components;
false keeps the earlier explicit compound behavior. `p.solid:1` additionally
requires one connected closed shell and one positive-volume valid solid.
Duplicate payloads, non-manifold multiple edges, deleted faces and degenerate
shapes reject. Sewing does not fill arbitrary gaps or discard failed geometry.

The report exposes connected components, shells, contiguous/multiple/free
edges and each free boundary's native length and endpoints. `closed` describes
the absence of free edges; it alone does not assert one connected solid.

## Loft correspondence and guide evidence

```json
{"op":"feature.add","type":"loft","name":"Skin","p":{"solid":0,"ruled":0,"compatible":0},"native":{"sections":["Low","High"],"guides":["Rail"],"controls":{"section_alignment":[{"reverse":false,"seam_edge":0,"seam_angle":0},{"reverse":false,"seam_edge":0,"seam_angle":0}],"guide_samples":17,"max_guide_deviation_mm":0.05,"require_ordered_contacts":true}}}
```

Each optional `section_alignment` row corresponds to one section. `reverse`
changes directed wire orientation. `seam_edge` changes the first edge of a
closed profile. `seam_angle` changes the parameter seam of a single closed
analytic circle, in degrees; other curve types reject an angular seam rather
than ignoring it. These controls change correspondence without relocating the
section's geometric support. Semantic edge names retain their authored source
identity through reordering. Point tips remain supported at un-guided ends.

Guide controls are available only with guides. They validate exact guide-to-
section contact against the fixed `1e-6` mm tolerance, directed section order,
coherent first/last endpoints and consistent cyclic contact order. The guide
surface check samples 2..129 checkpoints per native edge and rejects observed
deviation above the declared limit in `1e-6..0.05` mm. This limit governs a
measured rejection check; it does not enlarge geometry tolerances. The report
explicitly marks the global between-samples bound as unverified. Existing
lofts without new controls retain their earlier acceptance policy and expose
the new measured diagnostics without retroactive stricter rejection.

Guided construction remains a bounded section-generation loft, not a general
Gordon/G2 boundary-surface solver. Native smoothing, maximum degree,
compatibility and continuity settings now reach the guided kernel builder.
Neighboring-face tangency/curvature qualification and general self-intersection
proof remain unavailable. `silhouette_bounds_mm` is the exact axis-aligned
B-rep bounding envelope; it is not a certified view-dependent silhouette.

## Exact collision inspection

`inspectNativeCollision(a,aWorld,b,bWorld)` is a read-only worker-backed pair
inspection for valid, single, positive-volume exact solids. Both placements
must be rigid right-handed frames. Invalid B-reps, multiple solids, stray
faces, nonfinite coordinates, scaling and altered modeling tolerances reject.

The report distinguishes `separated`, `touching` and `interference`, measures
`min_distance_mm` and exact Boolean `common_volume_mm3`, and returns nearest
world points and B-rep hashes. A positive common volume is interference even
when bodies are fully nested and have no boundary crossing. `touching` uses
the disclosed fixed `1e-6` mm contact classification tolerance and has no
positive-volume common. No meshes are used to repair an exact failure.

This is a static geometric pair check. It does not prove a collision-free
trajectory, simulate response dynamics, qualify manufacturing or supply a
physical contact force. Mesh pair inspection is separately labelled with
triangle authority and its own limits.

`inspectMeshCollision` validates explicit rigid frames and indexed triangles,
then uses a BVH to prune triangle pairs. Bounds overlap only selects candidates.
Real triangle edge/face/edge distances detect transverse and coplanar boundary
contacts within a fixed `1e-6` mm classification tolerance. Every candidate is
checked; exceeding the input, traversal or pair capacity rejects the complete
request without publishing a partial answer.

Mesh `surface_intersection` reports boundary contact. Its `interference` and
`touching` fields are null because common material volume has not been measured.
With no boundary contacts, closed consistently oriented indexed meshes receive
solid-angle winding checks at a representative of each connected surface
component. Nested components report `interference`; open/non-manifold meshes
retain unknown containment and `surface_separated`. No minimum distance or
common volume is invented: those fields are null. Winding containment assumes
non-self-intersecting input surfaces; the report discloses that self-intersection
and physical response are unverified. It remains triangle authority, never
exact B-rep authority or manufacturing certification.

## Skeleton patterns from native seeds

The existing Skeleton `pattern` declaration retains its editable count and XYZ
step expressions and stable generated feature ID. When its seed already has a
native B-rep, evaluation uses the native linear pattern operation and retains
all copies as B-rep solids. A count of one uses an identity native transform;
counts of two through 64 use the native pattern. Larger counts reject explicitly
and roll back the atomic edit. The historical defaults remain three copies with
a 20 mm X step. Copies are kept separate rather than silently fused; overlapping
copies do not constitute a measured physical or manufacturing qualification.

## Local verification scope

Exact face and edge selection uses the same ordered B-rep topology as the
display spans and polylines. `face_canonical` and `edge_canonical` map repeated
uses of the same exact entity to its first display index; existing indices and
mesh buffers retain their order. Producer semantic names and merged/deleted
aliases remain intact and take precedence when available. Missing names receive
`@ark.face.<brep-sha256>.<index>` or `@ark.edge.<brep-sha256>.<index>` handles,
with `reference_scope: current_brep` and `persistent: false`.

Loft history binds each source edge to the actual edge retained by the assembled
wire. When numbered members resolve to distinct exact faces or edges, their
historical stem alias remains present as `split`, with empty `indices`, a reason,
`merged_from` and `split_into`. Selecting that stem rejects atomically; select an
individual bound member. A genuine merged alias still binds one exact entity
and retains its source membership through supported operation history.

These fallback handles bind one complete serialized B-rep identity. Any B-rep
change, including a dimensional edit, makes an old handle obsolete rather than
retargeting it to the same integer index. An identical B-rep identity can restore
its verified index. Descriptors come from the current B-rep, not incoming
metadata. Generic handles are deliberately less associative than producer
semantic names. Authored sources containing them require document format 35;
adding derived picking metadata alone does not advance the document format.
Inventories reject above 512 semantic rows or 16,384 combined rows, including
obsolete aliases, instead of publishing partial coverage. Degenerate edges with
empty display polylines receive no selectable fallback handle.

`tests/native_geometry_019b_tests.cpp` compares helix length/rise and sweep
volume to analytic values; exercises open loft consumption, fixed-tolerance
closed/open/disconnected sew and free-boundary lengths; checks real circular
seam changes and strict guide limits; and compares separated/touching/
overlapping/contained box collisions to analytic distance and volume. It also
checks hostile inputs, rigid-frame rejection, editable expression
recomputation, source persistence and document/revision rollback.
Native Skeleton tests cover count changes from four to five, source reload,
identity at one copy, historical pattern defaults and rejection at 65 copies.
Selection tests cover raw surfaces, sewing, guided and unguided lofts, primitives,
Boolean walls, native pattern copies and a synthetic STEP import; current-handle
idempotence, exact-identity restoration, forged descriptors, capacity boundaries,
source persistence and atomic rejection after an upstream B-rep change.

The iteration verification receipt records executed test results. A syntax
check or test fixture alone is not integration, deployment, manufacturing or
human acceptance evidence.

`tests/mesh_collision_tests.cpp` independently exercises shared-index closed
cubes, rigid transforms, nested components with no boundary crossing,
coplanar/penetrating surface triangles, overlapping bounds without triangle
contact, open/non-manifold unknown containment, hostile inputs and explicit
input/pair capacity rejection. Its standalone local execution passed; the
iteration receipt records integrated tool/runtime checks separately.
