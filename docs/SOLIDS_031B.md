# 031B exact solid modeling — B.02 / B.03

Status: **Verificado no escopo** local described and tested below. B.02/B.03
remain **Parcial** beyond that bounded scope. Manufacturing qualification and
performance comparisons are outside this evidence.

All operations use the existing revision-bound `doc_apply` → `native_solid`
path. Geometry comes from OCCT exact B-reps. The display mesh never chooses
the tool, retained piece or reference. New persisted recipe semantics select
document source43; older recipes retain their existing minimum source version.

## Support-bound editable recipes

Each operation takes one exact single-solid `inputs:[source]`,
`native:{op,source_face}` and driving expressions in `p`. `source_face` must
resolve to one current exact planar face. Optional `native.source_topology`
pins the complete source B-rep digest and rejects drift. Omitting the pin
retains associative producer-semantic face references after parameter edits.

The support frame origin is the selected plane's native origin; X is its
native plane X axis, Z is the **outward oriented face normal**, and Y is Z×X.
`p.cx` and `p.cy` offset that origin. Read exact face descriptors/frame data
before placing a recipe. Positive additive height follows outward Z; cuts
travel inward. The exact footprint must intersect positive area of the actual
trimmed support face. Disconnected or zero-effect additions reject. Every
result must remain one valid positive-volume solid.

| `native.op` | Required positive expression parameters | Optional expressions and bounded geometry |
| --- | --- | --- |
| `rib` | `length, thickness, height` | Rectangular prism centered on local X/Y. |
| `gusset` | `length, thickness, height` | Right-triangular prism from X=0 to length, Z=0 to height; centered through local Y thickness. |
| `mount_pad` | `width, depth, height` | Rectangular pad centered on local X/Y. |
| `boss` | `radius, height` | `bore_radius` defaults to0; must be smaller than radius. |
| `screw_support` | `radius, height, bore_radius` | Annular support; bore_radius must be positive and smaller than radius. Bore stops at the support plane; source drilling is a separate explicit hole operation. |
| `nut_seat` | `across_flats, depth` | Regular hexagonal antirotation pocket, optional `angle` in degrees. Optional positive `bore_radius` needs explicit `bore_depth>depth`; bore_radius must be smaller than half across_flats. |
| `lid_lip` | `width, depth, wall, height` | Centered exact rectangular ring;2×wall must be smaller than width and depth. |
| `lid_groove` | `width, depth, wall, groove_depth` | Matching rectangular ring pocket with explicit independent fit dimensions. |

`nut_seat` and `lid_groove` reject when the complete cutter is not contained
in the stock. No side/bottom breakout is silently accepted. Hex seats specify
nominal geometry and antirotation; undercut retention, insertion path, nut
standards, fit, load capacity and production qualification are not inferred.
Ribs/gussets are prismatic on planar supports; automatic draft, conformal ribs,
curved-support bosses and automatic wall attachment are outside this domain.
Lid fits are caller-authored dimensions, without an invented clearance standard.

Example after inspecting a current source's `z_max` semantic face:

```json
{"op":"feature.add","type":"native_solid","name":"Editable screw support",
 "inputs":[1],"native":{"op":"screw_support","source_face":"z_max"},
 "p":{"cx":"SupportX","cy":"SupportY","radius":"6",
      "height":"SupportHeight","bore_radius":"2"}}
```

`native_inspect.report.solid_recipe` records the measured before/tool/after
volumes, actual material change, support frame, subtractive containment scope,
unchanged Boolean tolerance and explicit unqualified manufacturing status.
Producer face/axis references propagate from exact tools and the source body.
Repeated recipes receive unique namespaces (`boss`, `boss2`, …), reported as
`reference_namespace`, so another support does not overwrite earlier aliases.

## Variable-radius fillets and chamfers

`native:{op:"fillet",edges:[semantic_edge],radius_law:[...]}` supports2..16
stations `{fraction:<expression>,radius:<expression>}`. Fractions must increase
strictly from an explicit0 to an explicit1; every radius is positive.
The measured domain is **one explicitly selected straight edge forming one
isolated OCCT contour**. Tangent chains, circular edges, zero endpoints and
multi-edge laws reject. Fractions follow the semantic edge tangent and the
worker reverses the OCCT law when needed. `p.radius` cannot coexist with the
law. OCCT constructs the varying fillet surface; there is no mesh approximation
or automatic reduction of radii on failure.

```json
{"op":"feature.add","type":"native_solid","inputs":[1],
 "native":{"op":"fillet","edges":["edge.x.ymin.zmin"],
   "radius_law":[{"fraction":0,"radius":"StartRadius"},
                 {"fraction":0.5,"radius":"MiddleRadius"},
                 {"fraction":1,"radius":"EndRadius"}]}}
```

Chamfers retain the legacy symmetric distance mode. Advanced modes require
`native.reference_face` adjacent to every selected edge, eliminating implicit
face-side choice:

* `native.mode:"distance_distance"`, `p.distance` and `p.distance2`.
* `native.mode:"distance_angle"`, `p.distance` and `p.angle` strictly between0
  and90 degrees. The angle is passed to OCCT in the reference-face convention.

Incompatible inactive fields reject. `report.edge_finish` records the chosen
mode and exact selected edge count. Geometry failure rejects the mutation.

## Exact splitting and retained material

`split_surface` takes `inputs:[source,tool]`, `native.tool_face` naming one
exact face of the tool and `native.retain:[piece_id,...]`. The actual finite
trimmed face is the splitting surface; it must span a complete source section.
Planar and closed cylindrical examples are tested. OCCT surface intersection
support remains the boundary; arbitrary NURBS arrangements are not claimed.

`split_tool` takes the same two inputs and uses the complete single-solid
exact tool. It partitions source material, including the source/tool overlap;
it does not implicitly choose cut versus common. Only the requested retained
pieces enter the result. Both operations require at least2 and at most64
valid positive-volume pieces before selection. Exterior/tangent/no-op tools
reject. Callers must explicitly list unique in-range retained piece IDs.

Piece IDs are zero-based positions ordered lexicographically by exact volume
centroid X/Y/Z, then volume. They express the authored selection rule and may
change after a topology-changing source/tool edit; inspect again before changing
retained pieces. Results report the complete piece inventory including exact
bounds, centroids, volumes and retained flags. Source, partition, retained and
explicitly removed volumes are recorded. Conservation uses the pre-existing
fixed volume check `max(1e-5 mm3, source_volume×1e-7)`; no tolerance inflation
or dropped failure pieces is allowed.

Exact source reference history is resolved separately against retained pieces.
A surviving unique face/edge retains its source alias; a source reference
spanning retained pieces is `split` with explicit `split_into` descendants.
Bound aliases use `piece.<id>.<source_reference>` and an additional index when
one piece has several descendants. Subsequent splits allocate `piece2`,
`piece3`, … namespaces and report the chosen `reference_namespace`, preserving
earlier aliases. Missing material produces `deleted`, never
silent retargeting. Lineage is capped at512 semantic rows and rejects excess.

The existing `split_plane` retains positive/negative/both authoring semantics.
It now reports both side volumes and solid counts, conservation error/limit,
retained volume and explicitly removed material. Both halves are evaluated
even when retaining one side. Exterior/tangent and conservation failures name
the failing side or measured source/partition volumes. For both retained sides,
exact semantic children are also exposed as `positive.<reference>` and
`negative.<reference>`.

## Verification record

`tests/solids_031b_tests.cpp` exercises analytic volumes for all eight recipes,
reversed outward normals, support custody, complete blind cutter containment,
invalid/inactive fields, variable fillet laws, explicit-face advanced chamfers,
planar/cylindrical exact splitting, retained/discarded volume accounting,
source-reference lineage, plane diagnostics, expression-driven agent authoring,
source43 persistence/downgrade rejection and atomic/stale mutation rejection.

Observed locally on2026-10-16: the pinned OCCT8/LLVM-MinGW Release target
`ark-solids-031b-tests` builds and the executable passes every stage above.
The first runtime attempt failed because split reference metadata retained a
JSON null type and omitted `split_into`; fixing the metadata initialization
restored exact lineage and the full test passed. No failed geometry case was
accepted by increasing tolerance, reducing radii or dropping solids.
Final review added unique namespaces for repeated exact splits and its regression
case; the final integration rerun passes that addition and all 116 native CTest
cases. The portable suite also passes all 76 cases.

The generated-result pipeline has local regression checks for complete exact
face/edge/vertex custody and explicit rejection of mismatched digests,
out-of-range producer bindings and malformed serialized payloads.

The separate `ark-contracts-031b-tests` covers input retargeting and its durable
journal, observed component-resource links, typed reference-query contracts and
rigid assembly condensation. Its results are recorded by the031B integration
verification. This is local synthetic evidence, without deployment, production,
supplier integration or manufacturing qualification claims.
