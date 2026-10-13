# Spatial sketch constraints

018B adds an equality solver for the authored points of native spatial curves.
The source remains editable and associative; OCCT constructs and validates the
resulting exact wire. This extends the earlier expression-driven coordinates and
line/arc dimensions. It does not certify a globally unique solution or provide
every constraint supported by a commercial CAD sketcher.

Studio exposes **All tools → 3D sketch constraints** for a selected curve.
Create an interpolating spline with **Interpolating spline 3D** and use it as a
sweep path. Existing curves with constraints or linked endpoints open their
retained source program when a simpler form cannot preserve its fields.

Point handles are `start`, `segment.N.end`, `segment.N.through`,
`segment.N.pole.M` and `segment.N.point.M`; N and M start at one. Spline/NURBS
ends alias their last control when there is no separately authored endpoint.
Implicit coincidence relations keep consecutive segments connected. An existing
line `length` also becomes a retained dimension during solving.

| Constraint | Additional fields |
| --- | --- |
| `fixed` | `point`, `value: [x,y,z]` |
| `coincident` | `a`, `b` |
| `distance` | `a`, `b`, `value` in mm |
| `on_plane` | `point`, `plane` (builtin or named construction plane) |
| `on_axis` | `point`, `axis` (named construction axis) |
| `parallel`, `perpendicular`, `equal_length` | `a,b,c,d` defining two directions |
| `angle` | `a,b,c,d`, `value` in degrees, strictly between 0 and 180 |
| `coplanar` | `a,b,c,d`; the first three must define a plane |

Numeric coordinates remain solver variables. Expression strings and coordinates
resolved from `start_ref`, `to_ref` or `through_ref` anchors are driven. Optional
constraint IDs must be unique. Dimensions accept named expressions. There are
at most 64 authored point handles and 128 explicitly authored constraints.

```json
{
  "start": ["0", "0", "0"],
  "segments": [{"kind": "line", "to": [30, 10, 5]}],
  "constraints": [
    {"type": "on_plane", "point": "segment.1.end", "plane": "XY"},
    {"type": "distance", "a": "start", "b": "segment.1.end", "value": "Length"}
  ]
}
```

Supply this as a curve feature's `native` source in revision-checked `doc_apply`.
`native_inspect` reports `spatial_constraints`: residuals, implicit relations,
iterations, local Jacobian rank and remaining degrees of freedom. The damped
solver has finite iteration and coordinate limits; incompatible or nonconvergent
constraints reject the entire edit. It never silently frees driven coordinates.

Constraints address authored point handles on segment curves. Surface splines,
included/projected curves and equation curves have their own source authority;
unsupported spatial-handle constraints on those operations reject explicitly.
Arc radius/normal and spline weights remain authored geometric fields rather
than additional solver variables. Degenerate directions reject. Tangency,
curvature, inequalities and general curve/surface incidence are outside this scope.

Source semantics require document version 34. Save/load retains the source rather
than its solved request. Older runtimes must reject that version explicitly.
See [construction anchors](CONSTRUCTION_DATUM_AUTHORING.md) and
[entity and sweep contracts](SKETCH_ENTITY_AUTHORING.md).
