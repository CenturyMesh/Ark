# Native surfacing

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
