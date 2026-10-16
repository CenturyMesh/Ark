# Triangle mesh authoring — 031B

B.03/D.04 remain **Parcial** beyond the bounded local scope below. These tools
retain indexed triangle authority, editable derived features, explicit input
sampling and the existing revision/atomic publication rules. No operation
creates an exact B-rep or qualifies manufacture. Sources and their retained
reference data stay in the document; editing a derived result does not overwrite
the imported source.

## Read-only self-intersection diagnosis

`mesh_inspect{id,self_intersections:true}` requests actual triangle contact
diagnosis. The C++ API is `inspectMeshSelfIntersections(const Mesh&)`. Its
`self_intersection_pairs` counts contacts outside each pair's shared indexed
vertex or edge. Proper adjacent faces are legal; coplanar overlap, duplicate
triangles and nonadjacent contact are reported. The result includes up to 32
triangle-pair examples, an explicit examples-completeness flag, complete-check
status, tested-pair count, fixed contact tolerance of `1e-7 mm`, and triangle
authority. This is a finite-coordinate triangle contact check, not an exact
arithmetic proof, a containment test, or manufacturing qualification.

At most 12000 triangles and 1000000 candidate pairs are accepted. Invalid,
nonfinite or degenerate triangles and exhausted limits reject the entire
diagnosis; a truncated example list does not truncate the contact count. The
ordinary topology audit remains available without running this optional check.

## Common editable recipe

Use `feature.add` with `type:"mesh_edit"`, `inputs:[source]`, an observed
`expected_revision` on `doc_apply`, and
`native:{op,source_topology:<mesh_inspect topology fingerprint>,...}`.
The fingerprint guards ordered indexed connectivity while allowing source
coordinate changes to remain associative. New 031B operation names conditionally
require source format 43; legacy mesh recipes retain their existing format.

Every new operation rejects nonmanifold/inconsistently oriented, degenerate,
nonfinite or self-intersecting input and rechecks output triangle contacts.
It also checks connected vertex fans: two shells sharing only one indexed
vertex cannot pass as a manifold solid solely because all edges have two faces.
Closed sources retain closure and signed-volume orientation. Optional
`native.require_closed:true` requires the result to close; unlike legacy direct
displacement this permits an open boundary to be explicitly filled or bridged.
An exact source requires `p.sample:1`, and the resulting report discloses sampling.
An advanced operation never silently increases a tolerance, drops a component,
or accepts partial geometry after a failed check. Unknown operation fields and
parameters reject.

## Protected remeshing

```json
{"op":"feature.add","type":"mesh_edit","name":"Refined mesh","inputs":["Source"],"native":{"op":"remesh","source_topology":"<observed>"},"p":{"target_length":2,"iterations":3}}
```

`remesh` performs conforming midpoint refinement. Every original vertex and
edge is protected: edges may be subdivided collinearly, and adjacent triangles
share each new vertex. Borders and creases therefore keep their geometry.
This scoped algorithm refines long edges without smoothing, collapsing short
edges or promising isotropic triangle quality. `target_length` is mandatory
in `[1e-6,1e6] mm`; `iterations` defaults to 3 and accepts integer 1..6.
The report names the algorithm and protection method, counts split edges and
retained vertices, measures maximum edge length and explicitly reports
`target_reached`. An iteration limit may leave that flag false. Exceeding 12288
vertices or 4096 output triangles rejects the feature atomically.

## Simplification with a measured sampled error cap

`native.op:"simplify"` requires `p.ratio` in `[0.05,1]` and `p.max_error` in
`[0,1e4] mm`. Interior quadric midpoint collapses preserve existing boundary
vertices/edges and manifold topology. The tool then measures nearest-triangle
distance from seven samples per triangle—vertices, edge midpoints and centroid—
in **both** source-to-result and result-to-source directions. A measured
deviation above the caller's cap rejects the entire result.

The report records `sampled_bidirectional_deviation_mm`, `max_error_mm`,
`error_cap_satisfied`, the sampling method and actual point-triangle term count.
`global_hausdorff_bound:false` is explicit: a finite sample cap is not a
continuous surface error guarantee. The measurement accepts at most 12000000
point-triangle terms, otherwise rejects. `target_reached` is false when protected
topology prevents the requested triangle count; that limitation is visible in
the report. No reduction is guaranteed for a minimal mesh.

## Explicit nonplanar hole fill

`native:{op:"fill_nonplanar",source_topology,method:"centroid_fan",loop:[...]}`
selects one complete ordered boundary loop of 3..256 unique vertex indices,
following its existing oriented boundary edges. The caller explicitly chooses
a piecewise planar C0 fan through the boundary vertex centroid. This does not
infer a fair or tangent continuous surface, nor replace exact `surface_fill`.

The result reports the explicit model, boundary count, remaining boundary edges
and measured maximum boundary deviation from its centroid/Newell mean plane.
Ambiguous vertex fans, invalid winding, degenerate/overlapping caps, capacity
limits or contact with existing triangles reject. In particular, capping the
opposite side of a lone flat triangle is rejected as coincident geometry.
Existing `mesh_fill` retains its planar ear-clip behavior and fixed tolerance.

## Region bridge

`native:{op:"bridge",source_topology,loop_a:[...],loop_b:[...]}` joins two
disjoint complete oriented boundary loops with equal vertex counts (3..256).
Each list follows its source boundary winding. Correspondence pairs `a[i]`
with `b[-i]`, preserving both explicit first vertices; this cancels both source
boundaries with consistently oriented triangles. The caller chooses each start
vertex. The implementation does not guess alignment, resample mismatched loops,
or remove a source patch. A twisted/overlapping bridge rejects through actual
triangle contact validation. Output is limited to 4096 triangles.

## Projection to a retained immutable reference

`native:{op:"shrinkwrap",source_topology,reference_mesh:{positions,indices},vertices?:[...]}`
projects all vertices or the explicit unique vertex region to nearest points on
the embedded triangle reference. `p.max_distance` is mandatory, in
`[1e-7,1e6] mm`; optional `p.strength` defaults to 1 and is in `[1e-6,1]`.
The nearest reference distance must fit the cap before applying strength;
otherwise the entire feature rejects.

The bounded reference payload is retained with the authored feature and survives
save/load unchanged. It is an immutable triangle snapshot, without an inferred
exact CAD authority or a live file link. Exact geometry may only become such a
reference through a separately explicit sampling workflow. Reference validity
and self-contacts are checked before projection. Up to 12000000 point-triangle
terms are allowed. The report records reference authority, projected vertex
count, maximum displacement, strength, cap and actual term count. Projection
does not promise topology repair, a solid, fairness, constant offset or
manufacturing fit.

## Local evidence

`ark-meshes-031b-tests` covers actual noncoplanar and coplanar crossings, legal
shared simplex adjacency, overlaps that share an indexed edge/vertex, invalid
indices, candidate exhaustion, protected border/crease refinement and measured
volume, explicit iteration/capacity limits, exact sampling, simplification
deviation acceptance/rejection, nonplanar fill, correspondence bridge and
twisted bridge rejection, immutable reference projection/selection/strength,
source43 persistence and old-format rejection, and multi-command rollback.
These are synthetic local regressions. No user document is a fixture, and this
scope makes no cross-product integration, production or manufacturing claim.

Local native execution on 2026-10-16 passed `ark-meshes-031b-tests` and the
existing `ark-mesh-model-tests`. An initial regression exposed the legacy
decimator accepting a closed tetrahedron's collapse into two coincident
opposing triangles. Candidate acceptance now also preserves signed-volume
orientation and at least four triangles for a closed source; the minimal case
retains its geometry and reports an unmet reduction target. The final run also
passed the source43/rollback, singular vertex fan and original planar-fill
rejection cases. This is local kernel evidence, without manufacturing release.
