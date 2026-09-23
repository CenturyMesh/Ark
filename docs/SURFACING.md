# Native surfacing in ARK 0.8

Studio's **Native surfaces** Commands group and MCP `doc_apply` create OCCT
8.0.1 B-reps. The feature history stores the authored parameters and `native`
specification; the evaluated B-rep, report, and display mesh are derived and
recomputed. A failed operation leaves the previous document and revision
intact.

`loft` uses 2 to 32 saved sketch names in section order. Sections need
compatible edge order and topology. `solid` and `ruled` are explicit expression
parameters (`1` or `0`). Closed sections are required for a solid loft.

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
thickness expression. A zero or invalid result is rejected atomically.

```json
{"op":"feature.add","type":"thicken","inputs":[3],"p":{"thickness":"2"}}
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
