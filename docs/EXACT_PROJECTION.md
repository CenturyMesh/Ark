# Exact associative projection — 002B

Milestones **B.02**, **B.03** and **D.04**: exact 2D projected sketch geometry and
spatial included curves. This is a bounded authoring feature, not a general 3D
sketch constraint solver or manufacturing qualification.

## Sources and destinations

**Project geometry** creates a driven entity in a planar named sketch. It projects
normally onto the destination plane. **Include geometry 3D** creates an exact
spatial `curve` feature, retaining the source XYZ coordinates. That curve can be
used as a sweep path or loft section/guide where the consumer's domain permits it.

Both accept one named source sketch entity, or a producer-authored semantic edge
or single boundary of a planar face. Sketch entities can be lines, circles, arcs
(including sweeps over 180 degrees), interpolating splines and connected wires.
No solid/extrusion intermediary is required for a sketch source. A source sketch
must precede its 2D target; self/future references and indirect feature cycles
reject. Points, sampled offsets and ambiguous names are not exact curve sources.

For a placed part, `source_instance` and `source_copy` select its solved world
placement. Projection composes that placement once. Definition coordinates remain
the default when no physical instance is selected. A copy index must exist on the
named instance and belong to the chosen feature.

Projected geometry remains linked and driven. Edit the source dimensions or
sketch entity to change it. `sketch.patch` rejects direct changes to a projected
entity. Changing an instance pose, source literal or parameter recomputes its
projection and downstream geometry. Undo/redo and reopen retain the association.

## Command examples

Pass the observed `expected_revision` with each `doc_apply` batch. A failed batch
leaves the document and revision unchanged. Discover actual edge/face names using
`reference_inspect`; do not infer a semantic name from a display triangle.

2D native-feature projection into an existing sketch:

```json
{"op":"sketch.entity.set","name":"Target","entity":{
  "kind":"project","name":"rim","mode":"normal",
  "feature":"Cylinder","reference":"cap.end"
}}
```

2D projection of a source sketch entity:

```json
{"op":"sketch.entity.set","name":"Target","entity":{
  "kind":"project","name":"linked_curve","mode":"normal",
  "source_sketch":"Source","source_entity":"curve"
}}
```

The sketch/entity selector and feature/reference selector are exclusive.
`source_instance` and `source_copy` apply only to a feature/reference source.

3D inclusion of a source sketch entity:

```json
{"op":"feature.add","type":"curve","name":"Spatial curve","native":{
  "op":"project","mode":"3d","source_sketch":"Source","source_entity":"curve"
}}
```

For a native edge, use `inputs:[sourceFeature]` and
`native:{op:"project",mode:"3d",reference:edgeName,source_instance?,source_copy?}`.
An included spatial curve may preserve a closed circle or a nonplanar open wire;
consumer constraints, such as a closed sweep section and connected guide, still
apply. There is no 3D point/constraint editor in this feature.

## Authority, persistence and failures

The old edge projection used five sampled points and could turn a true curve into
four chords. The new path selects the actual edge from a digest-bound topology
registry. Lines project from their exact endpoints. Trimmed conics, Beziers and
B-splines use an exact rational B-spline representation; orthographic projection
is affine, so projecting its control poles while preserving weights, knots and
multiplicities preserves the whole curve. Spatial inclusion copies the exact wire.
Curve orientation, trimming and world placement are retained.

The projected B-rep feeds native profile construction and extrusion. Adaptive
polylines draw the preview and sketch overlay; they are never converted back into
production edges. Derived B-reps/samples are process-local caches. The document
stores source selectors and mode, not a cached mesh, B-rep or thousands of driven
coordinates. New selectors/modes require document version **28**, including
journaled documents. Legacy version-13 projection inputs recompute from their
source; cross-plane planar-face section behavior is preserved for the old mode.

Rejected cases include split/deleted or ambiguous references, mismatched registry
digests, missing/suppressed sources, dependency cycles, curved face sources,
multi-wire faces/holes and collapsed projections. Choose individual named edges
for a face with holes. Unsupported exact curve classes are rejected rather than
fitted. Preview is bounded at 4096 samples; normal sketch profile limits also apply.
Native projection requires the OCCT worker; a core-only build does not silently
substitute mesh geometry.

`projectGeometry` and `projectSketchGeometry` in `ark/sketch.hpp` supply the UI
preview. They return exact result metadata and world display polylines on a copied
document. Apply still requires the revision observed before preview; the viewer
does not numerically construct production projections.

## Verification and numerical receipt

`exact-projection` exercises circles, oblique ellipses, nonminor arcs, splines,
spatial sweep consumption, source literals/parameters, sketch chains, instance
copies/pose edits, undo/redo, reload, version gates and atomic rejection paths.
`modeling-workflow` retains the legacy planar-face section and line offset cases.

The rational-circle extrusion exposed a separate native volume integration defect:
radius 100 mm, depth 2 mm gave **62910.63522899586 mm³** with the nonadaptive routine,
versus **62831.85307179586 mm³** analytically. The same bias affected the mass query.
The existing C++ properties owner selects integration from the actual B-rep domain.
Rational 3D curves, rational face trims and freeform B-spline/Bezier surfaces use
Gauss-Kronrod with knot spans and relative target `1e-10`. Polynomial trimming of
analytic surfaces uses adaptive Gauss with target `1e-12`. Fully analytic surfaces
and trims retain the analytic Gauss rule. This chooses a numerical integration
method; it does not change or substitute production geometry. Solid centroid,
mass/inertia queries and frame mass accumulation share the same owner.

Unconditional Gauss-Kronrod moment refinement caused construction-frame to take
80.90 seconds. A bounded profile found a purely analytic hollow rail's moments
taking about 2547 ms per call, compared with 0.198 ms for the analytic rule. The
coped tube required adaptive integration: the legacy result 33305.9357502626 mm³
differed from the refined 33305.904123042 mm³; adaptive Gauss agreed within
`8e-8 mm³` and took about 3 ms for moments. After domain dispatch,
construction-frame passed in 1.82 seconds on the observed local run. This is a
specific measured regression recovery, not a general performance guarantee.

`native-volume-integration` checks analytic hollow-tube volume/centroid/inertia,
a separate smooth numerical integral for the cope volume and centroid, and a
rational disk's volume/centroid/inertia. Existing projection thresholds were not
widened. The estimate returned by OCCT is not a rigorous global geometry
certificate. Surface-area integration and the separate source-notch metric
helper were not changed by this repair.

```powershell
ctest --test-dir out/live-workspace-release -C Release -R "exact-projection|modeling-workflow" --output-on-failure
```
