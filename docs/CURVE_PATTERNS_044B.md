# 044B — chained curve patterns along wires, splines and surfaces

**B.03/B.04 — Verificado no escopo local; D.04 — Parcial.** The exact
`native_solid` op `curve_pattern` places located copies of one seed solid along a
path. 044B adds one continuous arc-length domain over several edges or a whole
wire, pitch spacing, closed loops without a seam duplicate, stable skips, Frenet
and support-surface frames, and a compact reference scheme for large counts.
A request with only the earlier fields (`reference`, `count`, `margin_mm`,
`offset`, `x_hint`) runs the unchanged code path: the B-rep bytes and reference
registry of a legacy single-edge pattern are compared against digests recorded
from the previous build.

## Request

Inputs are `[seed, path]` or `[seed, path, support]`. The seed and the path are
consumed (hidden) as before; a third support input stays visible.

```json
{"op":"feature.add","type":"native_solid","name":"Topstitch","inputs":["Stitch seed","Card outline","Card"],
 "native":{"op":"curve_pattern",
   "references":["edge.3","edge.0","edge.1","edge.2"],
   "pitch_mm":4,"pitch_mode":"fit",
   "orientation":"surface","support_faces":["@ark.face.<observed digest>.12"],
   "project":true,"offset_mm":0.4,"max_deviation_mm":2,
   "reference_scheme":"compact","named_copies":[0]}}
```

| Field | Meaning |
|---|---|
| `reference` | One edge name (legacy). With any 044B field it joins the new path code. |
| `references` | 1..512 edge names of the path feature, in any order. They must form one simple G0 chain; the first listed edge fixes travel direction and, on a closed loop, the seam (station 0 at its start). |
| neither | The whole path feature, which must be wire-only (a `curve` feature, a sketch entity included with `curve` `op:project`, a spatial spline, `intersection_curve`, `bridge`), or a spline `cable_route` centerline (`electronics.centerline`). A body with faces rejects. |
| `reverse` | Travel the chain the other way; a closed loop keeps its seam vertex. |
| `count` | 2..2048 stations. |
| `pitch_mm`, `pitch_mode` | Exactly one of `count`/`pitch_mm`. `fit` rounds to whole intervals and adjusts the pitch; `truncate` keeps the pitch and reports `remainder_mm`. |
| `margin_mm`, `start_margin_mm`, `end_margin_mm` | Open-interval margins (legacy `margin_mm` sets both). |
| `phase_mm` | Closed loop without margins: arc-length position of station 0. |
| `skip` | Station indices omitted from the result; copy names keep station indices. |
| `orientation` | `translate` (default without `x_hint`), `x_hint` (default with it), `frenet`, `surface`. Local +Z follows the tangent. |
| `x_hint` | Local +X target for `x_hint`; `frenet` uses it only when no station has curvature. |
| `support_faces`, `project`, `offset_mm`, `flip_normal`, `max_deviation_mm` | `surface` only. Faces of the third input, else of the path body; default all faces. Local +X follows the outward normal of the nearest face. `project` moves each station to that nearest point plus `offset_mm` along the normal and projects the tangent into the tangent plane. |
| `reference_scheme`, `named_copies` | `copies` (default) publishes `copy.<station>.<seed name>` for every copy and rejects above 512 semantic names; `compact` publishes them only for `named_copies` (default: the first kept station). |
| `offset` | Legacy translation of every station point in path coordinates. |

Frenet frames use the principal normal at curved stations; straight spans carry
the previous normal projected normal to the new tangent. Normals flip at an
inflection, as the Frenet frame does. On an edge or vertex shared by support faces
the normal is the normalized mean of those faces; an opposed pair rejects.

## Result and receipt

The result is a compound of located shapes that share the validated seed TShape,
so the saved B-rep grows by one location per copy (500 copies: 118 kB; 2000
copies: 469 kB in the local test). There are no per-copy booleans and no
implicit fuse: union the pattern into a body as a multi-solid boolean tool when
one solid is required. Overlapping copies keep their multiplicity in volume.

`report.curve_pattern` records `path_source`, `path_edges`, `closed`,
`closed_spacing`, `path_length_mm`, `max_chain_gap_mm`, `spacing_mode`,
`stations`, `copies`, `skipped`, `pitch_mm` (actual), `requested_pitch_mm` and
`remainder_mm`, margins or `phase_mm`, `orientation`, `frenet_carried_stations`,
`surface` (support face count, source, projection, offset,
`max_deviation_mm`, `mean_deviation_mm`, `max_deviation_station`,
`boundary_stations`), `reference_scheme`, `semantic_references`,
`copy_face_stride`, `copy_edge_stride`, `copy_order` and up to 64
`station_frames` with origin, tangent and local X. With the compact scheme, the
k-th face of the seed in copy j (kept-station order) is current-B-rep face
`j*copy_face_stride+k`; use `@ark.face.<digest>.<index>` or `geometry_query`
filters to select it. These handles change with the B-rep digest.

## Limits and explicit rejections

- 2..2048 stations; `pitch_mm` that yields more rejects instead of clamping.
- Selection inventory: copies × (seed faces + unique edges) + semantic names
  ≤ 16384. A box seed (18 entities) therefore allows 909 copies; a cylinder
  (6 entities) 2048. The rejection states the allowed count.
- Disconnected edges, branches, revisited edges, zero-length edges, margins
  longer than the path, an all-skipped pattern, contradictory orientation fields,
  `x_hint` parallel to a tangent, a support normal parallel to the tangent, and a
  deviation above `max_deviation_mm` reject the whole edit.
- Edge ends join only by a shared vertex or within the sum of their own vertex
  tolerances. No larger tolerance is introduced.
- Arc length uses tolerance-driven integration (1e-10) and a Newton refinement of
  the abscissa inversion, matching an independent composite-curve reference to
  1e-7 mm in the test.
- A cable route without an exact spline centerline (legacy point cables) rejects.
- Studio has no dedicated authoring panel for these fields yet; agents use
  `pattern_plan` (`mode:"solid"`) or `doc_apply`.

## Persistence

A 044B spec (any 044B field, no `reference`, or a third input) saves as document
source **version 47**; version 46 documents carrying them reject on load. Legacy
curve patterns keep version 19. Saved snapshots of patterns up to 2048 solids
restore through the existing validated snapshot path.

## Agent surface

`pattern_plan{mode:"solid"}` passes every field above (`support` names the third
input) and reports `curve_pattern_044b`. `agent_onboard{topic:"automation"}` and
`{topic:"solids"}` include the contract.

## Local evidence (044B branch, Windows, OCCT 8.0.1)

`ark-curve-pattern-044b-tests` (CTest `curve-pattern-044b`) passed:

- legacy single-edge B-rep and reference digests equal to the recorded ones;
- line + tangent arc + B-spline chain listed out of order: 9 stations within
  1e-7 mm of an independent composite arc-length reference; reverse starts at the
  chain end;
- closed periodic spline, 360 copies: chord length 0.9246..0.9252 mm around the
  loop including the seam (no duplicate), x_hint frames, compact names only for
  copy 0; whole-feature path equals the single-edge result; `phase_mm`;
- closed polygon from a whole feature with corners as stations; margins turn a
  loop into an open interval;
- pitch `fit` (100 mm / 12 → 9 copies at 12.5 mm), `truncate` (remainder 4 mm),
  margins and stable skip names;
- surface orientation: cylinder r20 projection with 1 mm offset (radius 21,
  deviation 5 mm), torus projection (exact nearest point and normal, deviation 3
  mm, seam-edge stations), orientation-only, `max_deviation_mm` rejection;
- Frenet principal normal on a circle, rejection on a straight line, x_hint
  fallback;
- rejections listed above;
- document contract: `pattern_plan` → `doc_apply` with a whole curve feature,
  pitch, support input and projection; the support stays visible; version 47
  round-trip and cold rebuild parity; version 46 rejection; legacy output stays
  version 19 with an unchanged native payload.

Measured worker time in process (`modelSolidNative`, cylinder seed r0.25 h1.5 on
a closed spline, includes validation, topology and display mesh): 500 copies
276 ms, 2000 copies 1.17–1.27 s. Through `doc_apply` with the worker process and
host mesh (cylinder r0.4 h3 on a 1078.5 mm closed curve): 500 copies 0.70 s, 2000
copies 2.88 s; cold reload and recompute 0.25 s and 1.20 s. These are single
local runs on one machine, not a performance qualification. Related suites
(native solids, features, caches, snapshots, corpus) also passed; see the
iteration record for the full-suite result.

Not verified: Studio display performance with thousands of copies on other GPUs,
manufacturing of stitched seams, and any comparison of design quality or cost.
