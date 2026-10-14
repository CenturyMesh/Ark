# Exact native vertex pull — 022B

Milestones **B.01**, **B.02** and **B.03** remain **Parcial**. The operation
moves one selected exact topological vertex of a bounded planar polyhedron.
The resulting surfaces are explicitly planar facets; this is not an inferred
smooth surface, display mesh deformation or manufacturing qualification.

## Selection and authored recipe

Every native-solid result publishes `topology.vertices`, an inventory of unique
exact B-rep vertices, available through `native_inspect{id}.topology.vertices`:

```json
{"name":"vertex.7","point_mm":[10,20,30],
 "source_topology":"<current exact BRep SHA256>","persistent":false}
```

`topology.vertex_inventory_complete` and `vertex_reference_scope` identify its
coverage and authority. The inventory uses the exact topological map, not
render tessellation indices or a nearest sampled mesh vertex. Inventory capacity
is 16,384 unique vertices. Larger bodies publish an explicitly incomplete empty
vertex inventory; they remain otherwise usable.

Use the observed vertex name and B-rep digest in a downstream source operation:

```json
{"expected_revision":12,"commands":[{"op":"feature.add",
 "type":"native_solid","name":"Pulled corner","inputs":[1],
 "native":{"op":"vertex_pull","source_vertex":"vertex.7",
           "source_topology":"<observed source BRep SHA256>"},
 "p":{"dx":1,"dy":2,"dz":3}}]}
```

Displacements are local feature XYZ millimetres and can retain named parameter
expressions. The source identity binds to that input's exact observed B-rep;
changed source topology rejects without retargeting. `vertex.N` names are current
handles, not permanent semantic IDs after arbitrary upstream edits. The stored
downstream recipe retains the selected input, digest and displacement. Editing
its displacement regenerates the result and preserves the original feature.
Another pull can select the current output's exact vertex inventory and digest.

The recipe requires document format **37**. Older documents retain their prior
minimum format. Undo, redo, reload and recompute preserve authored source.
Revision conflicts and failed geometry reject the whole `doc_apply` mutation.

## Supported exact domain

The input must be one valid closed outward solid with one manifold shell, at
most 128 unique vertices, 64 faces and 256 edges. Edges must be exact straight
nondegenerate segments. Each face must be a planar convex triangle or
quadrilateral with one boundary wire; curved faces, polygon faces of higher
degree and holed faces reject explicitly. Both input and output receive exact
self-interference checks, allowing validated faceted outputs to be edited again.

The displacement magnitude must exceed `1e-6` mm and be at most `1e6` mm.
Source shape tolerances must not exceed the fixed `1e-6` mm operation tolerance.
Distinct vertex collapse, face degeneration or orientation reversal rejects.

Unchanged faces retain their exact geometry. Incident triangles and incident
quads that remain planar are rebuilt as exact planar faces. A nonplanar
incident quad is split on the explicit diagonal between the two **unmoved
neighbors** of the selected vertex. The result therefore has an authored
faceted surface, and the receipt reports the number of split quads. No triangle
from the display mesh chooses these facets or the moved point.

Faces sew once at the fixed tolerance, with no free edges or non-manifold
multiple edges. The entire result must be one closed shell and one valid
outward solid. The declared face census must match the actual result. There is
no repair retry, tolerance increase, subset selection or discarded solid.

## Evidence and references

The native report contains `vertex_pull` with source point, current point,
displacement, observed source digest, incident face count, split quad count,
surface model, signed polyhedral volume, volume tolerance and sewing tolerance.
Both the input and output signed planar-face integrals are compared with exact
B-rep volume. The infinite-point classifier verifies outward orientation.
OCCT's exact Boolean argument analyzer checks self-interference before and after
the edit; `self_intersections_checked:true` applies to this bounded producer.

Unchanged semantic face references remain bound to their exact replacement
after sewing. Split incident faces explicitly retain `status:split` and
`split_into` replacement names such as `x_max.pull.1`. Incident source edge
references become `deleted` with an explicit reason; they do not silently
point to a different edge. Current B-rep selection handles become obsolete when
their digest changes. The result publishes a complete current face/edge
inventory and a new exact vertex inventory.

`tests/native_vertex_pull_tests.cpp` covers all eight box corners, independently
calculated added wedge volume, exact B-rep validity/self-interference, preserved
input package, moved vertex identity, split/deleted/unchanged reference evidence,
STEP roundtrip, stale digest, edge collapse, inverted geometry, zero displacement
and curved-source rejection. It also checks another pull of the moved vertex
and a different corner on the resulting faceted source, plus document version,
source reevaluation, revision conflict, rollback and undo.

Local OCCT 8.0.1 CTest runs passed `native-vertex-pull` (0.66 s) and the existing
`native-solid` regression (1.06 s). Visual gesture
validation is recorded separately by the integrated Studio tests. Deployment,
external-product feature parity, forming behavior and manufacturing
qualification are outside this evidence.

The kernel validation follows the primary OCCT interfaces for
[exact self-interference analysis](https://dev.opencascade.org/doc/refman/html/class_b_o_p_algo___argument_analyzer.html)
and [face sewing](https://dev.opencascade.org/doc/refman/html/class_b_rep_builder_a_p_i___sewing.html).
The pinned local OCCT 8.0.1 headers are the compiled interface authority.
