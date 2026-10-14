# Indexed mesh authoring — 019B

B.01/B.02/B.03 remain **Parcial**. Studio direct edits produce derived history
features rather than overwriting an imported mesh. The source remains editable
and the geometry retains indexed-triangle authority.

Inspect the source with `mesh_inspect`; retain its `topology_fingerprint`. Apply a
revision-checked command such as:

```json
{"op":"feature.add","type":"mesh_edit","name":"Raised patch","inputs":["Indexed source"],"native":{"op":"extrude","triangles":[2,3],"source_topology":"<fingerprint from mesh_inspect>","require_closed":true},"p":{"dx":0,"dy":0,"dz":"rise"}}
```

`extrude` duplicates the selected patch's vertices, moves its cap and joins each
oriented boundary edge with two wall triangles. It requires a proper patch with
a boundary; selecting an entire closed shell rejects. `displace` moves unique
selected `vertices`, or vertices incident to selected `triangles`, using the
same authored XYZ expressions. Supply one selection kind. Empty, repeated or
out-of-range indices, stale connectivity, zero displacement and nonfinite inputs
reject. The fingerprint covers vertex count and ordered triangle connectivity,
so coordinate-only source changes remain associative.

Closed sources preserve consistent closed edges and their volume orientation.
`require_closed:true` also requires a closed input. Open consistently manifold
indexed surfaces can be edited explicitly without asserting solid volume.
Degenerate/non-manifold results reject. At most 12000 input triangles are accepted,
with the existing bounded output/document capacity. Self-intersections are not
checked or qualified, and watertight topology alone does not certify manufacture.

Native B-reps require explicit `p.sample:1` before entering mesh operations.
Studio's **Sample and weld mesh** makes that conversion a visible separate feature;
no direct edit silently welds indices or converts a result back to an exact solid.
The operation report discloses source sampling, closure and unchecked properties.

Studio supports coplanar connected patches, individual vertices and edges. Drag
creates an isolated preview and a dynamic dimension. Tab enters source expressions;
Apply or completing a valid gesture publishes at the captured revision. Escape
cancels. Changing the drawing invalidates the gesture. Patch expansion compares
every neighbor to its seed normal (3°) and plane (1e-4 mm), and rejects more than
4096 triangles rather than truncating. Those display selection thresholds do not
enlarge the geometry validation tolerance.

Source recipes require format 35 and retain topology guards through save/load.
The local regressions compare shared-index cube extrusion volumes, coordinate
updates, closure, invalid selections, atomic rollback and unsupported old versions.
See the iteration receipt for executed runtime/control evidence.
