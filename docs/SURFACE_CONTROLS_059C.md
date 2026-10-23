# Exact surface controls — 059C

Scope: B.01 and A.02. Existing planar-solid `vertex_pull` remains separate.
These controls edit existing exact open, clamped, untrimmed BSpline faces with
at most 32 × 32 poles. Poles use zero-based `(u,v)` indices. Mesh vertices are
never used as production geometry. Solids, multiple faces, periodic/closed
surfaces and trimmed/holed boundaries reject explicitly.

`surface_control` requires the current `expected_revision` and a feature `id`:

- `inspect` returns the exact control net, face reference and B-rep SHA-256.
- `stretch_plan` requires that SHA, an explicit displacement bound and 1–128
  `{u,v,delta_mm}` edits. Duplicate poles reject.
- `candidates` requires source and target SHA-256, target feature, corner `(u,v)`
  and radius ≤1000 mm. It returns at most 64 exact vertices, sorted by distance
  then current exact index, with incident semantic edge references/derivatives.
- `glue_plan` requires an explicitly selected `vertex.N`, with an optional
  incident `target_edge` and `tangent_axis` (`u` or `v`). It moves the corner and
  optionally its adjacent handle, retaining the original handle length. It
  verifies the resulting point gap ≤1e-6 mm and the declared tangent angle ≤1°.

Coordinates are definition-local millimetres; occurrence transforms are never
inferred. All plans are read-only and return commands plus a `doc_preview` call.
Publication remains an explicit revision-guarded candidate apply. The authored
SurfaceEdit keeps the source and optional target as DAG inputs, together with
their exact fingerprints; changed geometry must be inspected and replanned.
The persisted recipe contains references, never a copied target B-rep.

The edit copies the existing exact BSpline basis, retaining degrees, knots,
multiplicities, weights and natural parameter bounds. OCCT validates the new
face and native interference check; an additional 9 × 9 Jacobian sample rejects
observed degeneracy. Sampling is not a proof of global injectivity. Glue is one
corner point and at most one unoriented tangent line, not full boundary G1/G2,
surface sewing, or manufacturing qualification.

Local evidence: standalone native test compiled against the patched OCCT and
current ARK native libraries passed 63 checks. It exercises a nonuniform rational
surface, basis equality, individual pole movement, valid exact results, immutable
source, bounded deterministic candidates, gap/angle/handle proofs, and rejection
of stale hashes, trim/closed/solid sources, duplicate/out-of-range poles, unknown
fields, enlarged tolerance and absent targets. Host integration tests are supplied
separately; integrated tool routing, persistence and UI verification remain
pending the root build.

Integration: include `surface_deform_059c.inl` inside `surfacing.cpp` after
`surfaceEditResult`, with `Geom2d_Line.hxx`, `Geom2d_TrimmedCurve.hxx` and
`BRepAlgoAPI_Check.hxx` included at translation-unit scope. Dispatch
`surface_control_net` → `surfaceControlNet059`, `surface_glue_candidates` →
`surfaceGlueCandidates059`, and `surface_deform_poles` → `deformSurfacePoles059`.
Host exports `surfaceControlSchema059` and `surfaceControl059`. Register native
test `surface_deform_059c_tests.cpp` against `ark-occt ark`, and host test
`surface_control_059c_tests.cpp` against `ark` with the native-worker dependency.
