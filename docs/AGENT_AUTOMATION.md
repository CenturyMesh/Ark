# Geometry and numerical tools for agents

The [verified-edit and multicore extension](ENGINEERING_AGENT_V2.md) adds required
postconditions, reusable preview candidates, physical material properties and
rigid-body physics. Its contracts supersede the earlier preview/compute details
below. Measurements at the end of this document describe the earlier snapshot.

022B retains 86 standalone tools. `agent_onboard{topic:"automation"}` returns the complete
`doc_apply` command reference on demand; initial tool discovery carries a shorter
description. Existing tools and documents remain supported. Linked native
solid/hole patterns require document version 19. New source-linked component
patterns/skips require version 37; earlier documents retain their existing
minimum format. See [component patterns](SPECIALIZED_PATTERNS_022B.md).

Agents create, edit, refresh or export 2D technical/manufacturing drawings only
when the user explicitly requests that drawing work. Drawing discovery is
read-only; it is not an automatic completion, BOM or DFM step.

## Read, evaluate, apply

1. Read `doc_info{detail:"design"}` for the current revision.
2. Inspect exact geometry with `geometry_query`, or group inspections in
   `query_batch` using `{tool,args,fields?}`. `fields` optionally selects top-level
   result fields. Batches permit a fixed read-only allowlist, no exports or
   recursive calls, and return timing and individual errors.
3. Evaluate edits with `doc_preview{expected_revision,commands}`. `valid:false`
   contains the rejected candidate's diagnostic; `valid:true` includes compact
   before/after views and an `apply` object. The live document, revision and undo
   history are unchanged.
4. Execute `doc_apply` with the returned `apply` object. A changed revision
   rejects it. The preview is evidence, not a reservation or a transaction lock.

Errors retain the existing text and revision and add `error_code`, `tool` and
`stage`. Command parsing errors identify the zero-based `command_index` and
`operation`. A geometry failure can instead originate in a downstream feature;
its diagnostic identifies that feature rather than attributing it to an
unrelated final command.

`fields` selects existing top-level result names. A missing projection rejects
with the actual available names; read those names and request the supported
field, or read the complete result once. Native `geometry_query` rows have
`ok`; wrapped tool results have `isError` and `structuredContent`. Unknown
measurements do not become zero. For `assembly_info` clearance, inspect
`clearance_checked`, `clearance_reason` when present, `separation_mm` and
`overlap_volume_mm3`; a guessed `clearance` field is not a measurement.

## Exact queries

`geometry_query{id,queries}` accepts a unique feature name or ID and 1..128
queries. It loads that B-rep once per batch. Each result has `index`, `ok`, and
either measurements or an error. Coordinates are in the feature's local mm
frame. Mesh-only bodies reject. Check each result's `ok` before using it.

```json
{
  "id": "Block",
  "queries": [
    {"action":"references","filter":{"kind":"face","normal":[0,0,1]},"unique":true},
    {"action":"properties","density_g_cm3":7.85},
    {"action":"section","origin":[0,0,15],"normal":[0,0,1]}
  ]
}
```

| Action | Inputs and evidence |
| --- | --- |
| `references` | `filter`: kind, analytic surface type, oriented normal, unoriented axis, radius_mm, near + within_mm, coaxial_origin, adjacent_to. All filters must match. Optional positive tolerance_mm and angle_tolerance_deg. `unique:true` rejects zero or multiple matches. Only bound/merged producer names are returned. |
| `properties` | Optional reference and density_g_cm3. Area, bounds, edge-length sum; solids also return volume centroid, volume inertia in mm5, principal moments/axes, and mass inertia in g mm2 with density. Tensor is row-major about the centroid in feature axes. Repeated principal moments make corresponding axes nonunique. |
| `curve` | Semantic edge reference, count 1..128, margin_mm or fraction 0..1. Samples use arc length and include point, tangent, curvature and curvature vector. Closed edges sampled at both endpoints repeat the seam unless margins are supplied. |
| `surface` | Face reference and optional native u,v. Returns point, oriented normal and curvatures. Defaults to the UV bounds midpoint; points outside the trimmed face reject, including a midpoint in a hole. Singular derivatives are explicitly undefined. |
| `point_distance` | point xyz; nearest boundary point and distance. Negative denotes inside exactly one solid. Multiple-solid bodies and wires return unsigned distance. |
| `continuity` | Edge references a,b and optional fraction_a/fraction_b (defaults 1/0). Checks local directed G0/G1/G2 using position, tangent angle and curvature vectors. Optional distance_tolerance_mm, angle_tolerance_deg and curvature_tolerance_per_mm. This is not a whole-surface continuity certificate. |
| `section` | Plane origin and normal. Each connected wire reports closure/length and, when closed, enclosed area, centroid and area inertia in mm4. Nested loops are reported independently; enclosed areas must not be blindly summed as material area. |

Reference identities are names tied to the producing B-rep. Numeric topology
enumeration and display triangles are not persistent selection handles.

## Math and positioning

`math_eval` provides add/subtract/dot/cross/distance/angle for vectors a,b;
norm/normalize; rigid frame construction from origin,z,x_hint; point/direction
transforms between from_frame and to_frame; plane projection and line-plane
intersection. Frames are right-handed, column-major 4x4 matrices. Degenerate
directions and non-rigid frames reject. Vector coordinates share caller units;
angles return degrees. Explicit `convert{value,from,to}` supports compatible
mm/cm/m/in, deg/rad and g/kg conversions, not arbitrary dimensional expressions.

`placement_plan{expected_revision,node,reference?,target,mode?,offset?}` evaluates
an edit for a free instance. The target is `{node|feature,reference?,copy?,
fraction?}`, `{plane:name}` or an explicit `{frame:[16 column-major values]}`. `align` maps the source frame to the target frame; `center` translates
area/volume centroids. The optional offset is expressed in target axes. It
returns a preview and apply payload. Grounded or mated source instances and nested placements in mated assemblies
reject; use existing assembly mates for persistent relations or group placement.

`pattern_plan` accepts expected_revision, path (feature name/ID), reference (one
edge), count, margin_mm, name, and a mode:

- `instances` duplicates a feature as assembly instances; optional x_hint makes
  local Z follow the tangent. `points` creates point datums. These are snapshots
  in path feature coordinates, explicitly `path_linked:false`.
- `solid` creates a native `curve_pattern` from inputs `[feature,path]` and
  returns `path_linked:true`. Copies form a compound, not a fused union, and
  publish `copy.N.<original_reference>` names. Overlapping copies retain physical
  multiplicity in volume properties.
- `holes` creates native `hole_pattern` with target feature, path, normal,
  radius, depth, and optional offset. Every cylindrical cutter must intersect
  the target. Count/margin/offset remain editable native expressions and
  regenerate with the path. Overlapping cutters are Boolean geometry, not
  an assertion of independent or manufacturable holes.

Linked patterns save version 19 and reject stale path names. Count is 2..128;
solid patterns also obey the 64-solid and 512-reference registry capacities. Inputs, offsets
and normals share part coordinates. There is no implicit assembly placement.

For persistent component placement, use `assembly.add`/`assembly.edit` with
`pattern.type` `linear`, `circular`, `pattern_driven`, `sketch_driven`,
`curve_driven` or `chain`. Source recipes retain expressions, source identities
and stable skips through history; counts are 2..32. The specialized source
contract, native guide bounds, actual XYZ handles, closed endpoint rules and
explicitly unchecked chain contact/motion domains are documented in
[022B component patterns](SPECIALIZED_PATTERNS_022B.md).

## Bounded design solving

```json
{
  "expected_revision": 2,
  "parameter": "width",
  "lower": 5,
  "upper": 14,
  "target": 3,
  "tolerance": 0.000001,
  "objective": {
    "tool":"assembly_info",
    "args":{"clearance":{"a":"Base","b":"Moving"}},
    "path":"separation_mm"
  }
}
```

`design_solve` evaluates isolated candidates and bisects a bracketed scalar
objective. It returns sample evidence and an apply payload only on convergence.
`max_evaluations` is 3..64 (default 32); max_seconds defaults to 30 and is at most
120, checked between operations. An in-flight native operation has its separate
30-second watchdog. Invalid geometry aborts the solve. No extrapolation, global
optimum or continuity across topology changes is claimed. `mode:"sensitivity"`
returns the secant over the supplied interval, not an exact local derivative.

For contact, inspect `overlap_volume_mm3` too: a zero separation can be either
contact or overlap. Legacy negative `clearance_mm` is an estimate based on the
reported method. `penetration_depth_checked:false` explicitly excludes an exact
minimum translation distance claim.

## Computation and validation

The document keeps a nonserialized process-local feature cache. Independent
primitives key their authored data and referenced parameter values. Complex
features conservatively include upstream generations, parameters, sketches,
datums, materials, assembly and mates. This can invalidate extra features; it
does not skip an unproven dependency. Tessellation settings also invalidate.
`doc_info`/`doc_apply` expose features_reused, features_evaluated and recompute_ms.
Cold loading still computes authoritative bodies from document intent.

On Windows, one resident OCCT process per calling thread reuses the existing
file protocol with event notification, a 30-second request watchdog and a job
object that terminates the child on host exit. Workers restart on errors,
executable/thread-budget changes, after 256 requests, or after 30 seconds idle. Set
`ARK_NATIVE_ONESHOT=1` for the process-per-call control. Other platforms retain
the existing one-shot transport. `kernel_info` reports transport, request and
process-start counts. A single AgentSession still requires serialized calls.

Sketch Jacobians use forward automatic differentiation. Sparse residual support
avoids zero products, and independent normal-equation components solve in
separate dense blocks. Rank remains a local numerical property. Assembly mates
retain their existing numerical solver. `linearizeSketch` exposes residuals,
free variable slots and derivative columns for numerical clients.

Run `scripts/build.ps1 -Test` and `scripts/build.ps1 -CoreOnly -Test`.
`agent-features` covers rollback/revision/undo isolation, cache vs cold rebuild,
vector units, derivative agreement with independent central differences,
semantic queries, section and inertia checks, placement, linked patterns,
target clearance solving, serialization, and worker error recovery. It prints
schema byte sizes and workflow elapsed time; these are not LLM token counts.

Local validation on 2026-10-09 passed 36/36 native CTests and 24/24 core CTests.
Three sequential runs per transport of the same final C++ workflow measured a
median 750.667 ms with the resident worker versus 2005.630 ms with one-shot
workers (2.67x for this workload). Process counters, including the terminal
kernel-info probe, reported 64 requests and 2 starts in resident mode versus 64
starts in one-shot mode; one resident restart intentionally exercises recovery
from a failed request. The initial tool schema is 28545 bytes versus 36755 bytes
with the full command description, a 22.3% reduction. Neither measurement is an
LLM token count or a guarantee for other models. Raw runs and source hashes are
in [the validation record](agent-automation-validation.json).
