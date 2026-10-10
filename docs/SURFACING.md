# Native surfacing

## Solid, surface and mesh workflow

Surfaces are exact OCCT geometry, independent of the mesh toolkit. Studio groups
their authoring tools separately. Use a sketch or exact face as the source,
construct/trim/offset the surface, then thicken or sew a closed shell into a
solid. An explicit sampled mesh operation can convert the display tessellation
into editable mesh geometry. This last step loses B-rep authority; ARK does not
claim automatic mesh-to-NURBS reconstruction.

`surface_edit` adds two version-26 operations, each with one exact input:

```json
{"op":"feature.add","type":"surface_edit","name":"Skin","inputs":["Block"],"native":{"op":"extract_face","face":"z_max"}}
```

Inspect semantic face names first. Extraction validates their B-rep digest and
preserves the original trimmed face, including holes. Missing, stale or
ambiguous references reject. The result publishes the semantic face `surface`.

```json
{"op":"feature.add","type":"surface_edit","name":"Trimmed skin","inputs":["Skin"],"native":{"op":"trim_plane","origin":[8,0,0],"normal":[1,0,0],"keep":"positive"}}
```

Alternatively use `native.plane` with a construction-plane name. The trim input
must be exactly one surface face, and the plane must divide it into one
connected face on each side. Both sides are checked for valid topology and
area conservation. Tangent/outside cuts, solids and disconnected patches
reject; this is not a general arbitrary-curve surface trimming command.
See [the runnable hybrid example](../examples/surface-hybrid.arkops).

The toolkit does not yet implement general boundary-surface G2 matching,
untrim/extend of arbitrary faces, mid-surface extraction, flattening of doubly
curved surfaces or automatic reverse engineering. Existing loft continuity
settings do not establish continuity to an arbitrary neighboring support face.

Studio's **Native surfaces** Commands group and MCP `doc_apply` create OCCT
8.0.1 B-reps. The feature history stores the authored parameters and `native`
specification; the evaluated B-rep, report, and display mesh are derived and
recomputed. A failed operation leaves the previous document and revision
intact.

`loft` uses 2 to 32 saved sketch names in section order. Sections need
compatible edge order and topology unless `p.compatible` is `1`, which asks
OCCT to rematch them. `solid` and `ruled` are explicit expression parameters
(`1` or `0`). Closed sections are required for a solid loft. A sketch that
contains only one point is a tip, and a tip can only be the first or last
section. `p.smooth` `1` turns on smoothing; `p.continuity` is `0`, `1`, or `2`
and defaults to `1`. The B-rep keeps face names `cap.start`, `cap.end`, and
`side.<entity>`, plus edge names `edge.start.<entity>`, `edge.end.<entity>`,
and `rail.<index>` for a later fillet or chamfer. `native.guides` names one to
eight open sketches. The sections follow those curves, which is the step
between a section loft and a sweep along one path. A ruled loft cannot take
guides, and a point tip cannot either. When `p.smooth` fuses several
`side.<entity>.<index>` faces into one, `reference_inspect` also lists
`side.<entity>` with status `merged`, the surviving face, and `merged_from`.
`native.faces`, `native.edges`, and `plane.set` `support.reference` accept
that alias. The saved document keeps the authored name. A loft with guides
saves as document version 11.

A sketch section may be an arc or a wire. An arc stores center, radius and
sweep angles in degrees. A wire is one entity of line, minor-arc and cubic
segments; each segment becomes a profile edge named `name.N`. A major arc
stays the five-degree-of-freedom arc. A `curve` feature stores `native.start`
and `native.segments` of `line`, `arc` or `nurbs`. Its wire B-rep is the
authority. The viewport shows a 0.3 mm ribbon, which is display evidence.
`sweep` `native.path` and loft `native.guides` accept that curve feature
name, and a loft section accepts a point datum. Sketch names win over datum
names, which win over feature names. `intersection_curve` sections one solid
with a datum plane and must produce one wire. `bridge` joins two semantic
edges. Continuity 1 is a cubic whose end derivative is the unit edge
tangent times the chord length. Continuity 2 is a quintic with the same
end tangents and zero end curvature; it does not copy the
source edge's sampled curvature, and it is not a surface blend. These
choices save document version 12. The product version stays 0.9.0.

```json
{"op":"feature.add","type":"loft","name":"Transition","p":{"solid":"1","ruled":"0"},"native":{"sections":["Base","Top"]}}
```

`surface_fill` takes one saved closed sketch through `p.sketch`, or an advanced
`native.profile` with an explicit 3D edge sequence. `bspline_surface` takes a
rectangular grid of 2 to 32 `[x,y,z]` poles per direction, with optional
positive weights. Degree expressions go in `p.u_degree` and `p.v_degree` and
must be lower than the pole count in that direction. The Studio editor starts
with a 4 by 4 JSON grid.

```json
{"op":"feature.add","type":"surface_fill","p":{"sketch":"Boundary"}}
```

`thicken` takes one preceding native face or shell ID in `inputs` and a signed
thickness expression. A zero or invalid result is rejected atomically. A closed
solid is hollowed with `native_solid` operation `shell`: signed `p.thickness`
and optional `native.faces` for the faces that open. Negative thickness builds
the wall inward. The result is one exact solid. A shell saves as document
version 11.

```json
{"op":"feature.add","type":"thicken","inputs":[3],"p":{"thickness":"2"}}
```

`sweep` takes two saved sketch names, or advanced profile objects, in
`native.section` and `native.path`. The section is carried along the path with
corrected Frenet framing. `p.solid` is `1` or `0` and defaults to `0`; a solid
needs a closed section. `p.correction` defaults to `1` and `p.contact` defaults
to `0`, so a section centered on the path start is not shifted onto the curve.
The path may be closed.

```json
{"op":"feature.add","type":"sweep","p":{"solid":"1"},"native":{"section":"Profile","path":"Spine"}}
```

`extrude_surface` takes `p.sketch` or `native.profile`, and a signed
`p.distance` whose absolute value must exceed 1e-6 mm. The direction is the
profile frame's +Z axis. `p.solid` defaults to `0`; a solid needs a closed
profile. `revolve_surface` uses the same profile, `p.angle` in `(0, 360]`
degrees, and `native.axis` of `X`, `Y`, or `Z` (default `Y`) through the frame
origin. A full turn is the closed revolution, not a 360-degree seam.

```json
{"op":"feature.add","type":"extrude_surface","p":{"sketch":"Rail","distance":"5","solid":"0"}}
```

```json
{"op":"feature.add","type":"revolve_surface","p":{"sketch":"Generatrix","angle":"360","solid":"0"},"native":{"axis":"Y"}}
```

`offset_surface` takes one preceding face or shell. Solids are rejected; use
`thicken` for a solid. `p.distance` is signed and must exceed 1e-6 mm in
magnitude. The report sets `offset_intersection_checked` to false.
`sew` takes 2 to 16 face or shell inputs at the 1e-6 mm tolerance. `p.solid`
defaults to `0` and is accepted only when the result is one shell with no free
edges. The report includes `free_edges`.

```json
{"op":"feature.add","type":"offset_surface","inputs":[4],"p":{"distance":"2"}}
```

```json
{"op":"feature.add","type":"sew","inputs":[4,5],"p":{"solid":"0"}}
```

Use `native_inspect{id}` for the compact OCCT report. It returns kernel version,
B-rep validity, area, volume when solid, and feature-specific checks without
transmitting the mesh or encoded B-rep. `export_step{id,path}` writes the
selected native feature's actual B-rep, then reads the STEP back for validation.
The viewport mesh is generated separately for display and selection.

These exact native shapes do not pass through legacy mesh Boolean, transform,
or sketch-cut operations. Edit their authored parameters and construction
sketches, or place a native part through an assembly instance. The earlier
parametric `x(u,v)` expression surface remains a separate mesh feature.

## Exact plane splits and copy-only mirrors

`native_solid` operation `split_plane` splits one exact solid by a plane.
`native.keep` is required: `positive`, `negative`, or `both`. Positive is the
side pointed to by the plane normal. Select `native.plane` (`XY`, `XZ`, `YZ`
or a named datum), `native.face` for a discovered planar semantic face, or
`native.normal` with `native.origin`. Built-in XZ has normal -Y; YZ has +X.
The plane must actually divide the solid; tangent and outside planes fail.
Keeping both halves returns a compound, not a fused body.

```json
{"op":"feature.add","type":"native_solid","name":"Upper half","inputs":["Blank"],"native":{"op":"split_plane","plane":"Cut datum","keep":"positive"}}
```

`mirror` continues to retain the original by default. Set `p.copy_only` to `1`
to return only the reflected exact geometry; `copy_only` and `fuse=1` cannot
be combined. This avoids displaying or assembling the original twice.

```json
{"op":"feature.add","type":"native_solid","name":"Left part","inputs":["Right part"],"native":{"op":"mirror","plane":"XZ"},"p":{"copy_only":1}}
```

Both additions use document version 15. References split or deleted by the
operation remain explicitly unresolved; agents must inspect the resulting
reference registry before attaching downstream operations.

## Sampled meshes

`mesh_body`, `mesh_weld`, `mesh_smooth`, `mesh_decimate`, `mesh_offset`, and
`mesh_fill` are triangle operations. Their result is mesh authority:
`exact_geometry` is false, `self_intersections_checked` is false, and
`native_inspect` rejects them. A feature that already has a B-rep enters one of
these operations only when `p.sample` is `1`; the sampled display mesh is then
the input and the result has no B-rep. `import_mesh` copies an STL or OBJ file
into a `mesh_body` and does not keep a live link to the file. Lengths are
millimetres. The operation limits, report fields, and the `mesh` onboarding
topic are in [the agent protocol](AGENTS.md).

## Spatial arc recipes — 005B / B.03

An authored `curve` feature carries an ordered exact spatial wire. The initial
point and every coordinate accept parameter expressions. Three-point arcs
specify a point on the intended interval:

```json
{"op":"feature.add","type":"curve","name":"Raised spine","native":{"start":[2,"radius",0],"segments":[{"kind":"arc","mode":"three_point","through":[2,0,"radius"],"to":[2,"-radius",0]}]}}
```

`mode:"tangent"` constructs a circle from the segment start, its endpoint and
an oriented start tangent. When omitted after an earlier segment, the tangent
comes from that exact edge's end derivative. The first segment requires an
explicit tangent; an explicitly supplied tangent on a later segment must
continue the previous edge's direction.

```json
{"op":"feature.add","type":"curve","name":"Tangent spine","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"arc","mode":"tangent","to":[20,10,0]},{"kind":"arc","mode":"tangent","to":[10,20,10]}]}}
```

Use `tangent:[tx,ty,tz]` when authoring a first tangent arc. Both recipes can
produce arcs greater than 180 degrees. An optional `normal:[nx,ny,nz]` verifies
the authored plane; inconsistent normals, repeated/collinear points, a zero
tangent and an endpoint on its tangent line reject. In recipe modes the normal
is an unoriented plane witness: either parallel sign is accepted, while the
ordered points/start tangent select traversal. Inputs do not silently
project onto another plane or substitute a polyline.

Existing `kind:"arc"` radius segments use `to`, `radius`, `normal` and
`sweep:1|-1`; `sweep` selects left/right of the directed chord. Optional
`major:true` selects the interval greater than 180 degrees, with the same
side convention. A 180-degree arc has one semicircle on each side. Recipe
`mode` cannot be combined with radius/sweep/major fields. New modes/major
semantics require document version 30; their original expressions persist.

These features remain exact OCCT circular edges, suitable for `native.path`
in a sweep or for loft guides. `geometry_query` with `action:"continuity"`
measures exact joins via semantic `edge.0`, `edge.1` references. Viewport ribbons
and samples are presentation. This is an expression-driven spatial curve
program, not a general spatial geometric constraint solver, surface blend,
or manufacturing qualification.

The native `ark-sketch-arc-tests` configuration checks three-point and radius
major-arc lengths, inherited spatial G1 tangency, coplanarity/reversal rejects,
parameter changes and cold persistence replay. Its sweep check compares the
exact solid's volume against the independent circular-spine area-times-length
formula. Current pass/fail evidence is recorded with the iteration release.
