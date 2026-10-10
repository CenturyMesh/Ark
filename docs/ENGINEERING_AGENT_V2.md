# Verified edits, rigid-body physics and multicore evaluation

ARK now exposes 53 tools. Existing document formats and modeling commands remain
supported. Documents with declared elastic material properties use version 20;
linked curve patterns retain version 19. The product version remains 0.9.0.

## Transaction conditions and reusable previews

`doc_apply` and `doc_preview` accept optional `checks`. Every check selects a
read-only query result using `{tool,args,path}`. Paths use dot-separated object
keys and numeric array indices. Comparisons are `ge`, `le`, `eq`, `between`,
`true` or `false`. Numeric inequalities/equality accept an explicit nonnegative
`tolerance`, default zero; `between` requires min/max. Missing, failed or
nonnumeric measurements do not satisfy a numeric condition.

```json
{
  "expected_revision": 7,
  "commands": [{"op":"param.set","name":"bracket_width","expr":45}],
  "checks": [
    {
      "id":"minimum_gap",
      "query":{"tool":"assembly_info","args":{"clearance":{"a":"Bracket","b":"Motor"}},"path":"separation_mm"},
      "op":"ge","value":3,"parameters":["bracket_width"],"diagnose":true
    },
    {
      "id":"maximum_mass",
      "query":{"tool":"physics_inspect","args":{},"path":"mass_kg"},
      "op":"le","value":0.5
    }
  ]
}
```

The complete candidate is evaluated before checking postconditions and before
the document/undo stack is changed. A failed condition returns
`POSTCONDITION_FAILED`, observed values, targets, required correction and query
references. Optional named `parameters` list editable candidates. With
`diagnose:true`, failed numeric checks can sample a local central difference:
at most four extra model evaluations in total, started within a five-second
diagnostic window. The reported linearized parameter delta is a suggestion,
not an automatically applied repair or proof of topology continuity. There are
at most 32 conditions; a 30-second budget is checked between condition calls.
Individual native calls retain their own 30-second watchdog.

A valid `doc_preview` normally returns a `candidate_id` and this apply payload:

```json
{"expected_revision":7,"candidate_id":"session-scoped-opaque-id"}
```

Submitting it to `doc_apply` reuses the evaluated document and verified
conditions. It makes no native geometry call in the commit itself and creates one undo entry.
Studio still refreshes display buffers. A bounded, nonserialized cache of
validated reference frames avoids repeated native inspection when rebuilding
assembly display placements; its identity includes the exact B-rep bytes,
topology registry, reference name and sample fraction.
`recipe` retains the expanded commands/checks for inspection. IDs are bound to
the session, document revision, complete authoring fingerprint and tessellation
settings. Changed or expired candidates reject; extra commands cannot be
attached to a prepared commit. The cache holds at most four candidates within
an estimated 128 MiB budget, with five-minute expiry. Oversized previews remain
read-only valid evidence but return `candidate_cached:false` and a recomputing
command payload. Mutations invalidate the session's prepared candidates.

## Materials and physical properties

`material.set` accepts either `density` in g/cm3 or `density_kg_m3`, never both.
Zero means unknown density. Per-feature `p.density` remains an expression in
g/cm3 and takes precedence over its named material. `materials_inspect` reports
the two density units, assignments, appearance and optional `physical` values:

```json
{
  "op":"material.set","name":"Declared alloy","density_kg_m3":2700,
  "physical":{
    "young_modulus_Pa":70000000000,
    "poisson_ratio":0.33,
    "yield_strength_Pa":250000000,
    "thermal_expansion_per_K":0.000023
  }
}
```

These are caller declarations. Studio's generic material presets are nominal
starting values, not certified grades. Elastic modulus/strength must be
positive; isotropic Poisson ratio must be between -1 and 0.5. Thermal expansion
is stored metadata; thermal deformation is not simulated.

`physics_inspect{id?}` selects one local feature or, without an id, all visible
assembly instances (including pattern copies), or unconsumed output features
when no assembly is present. Density and exact native volume are required.
Missing data returns `mass_checked:false` without a partial aggregate mass.
Construction frames aggregate member/profile densities and exact member inertia; their profile declarations remain the authority. Studio material assignment to a whole frame updates all its profiles. PCB mass remains unsupported without a per-material physical BOM, even if a display material is assigned.

The report includes:

- Mass, center of mass, and center of gravity under the declared uniform
  gravity vector. Zero gravity leaves the center of gravity undefined.
- Inertia in kg.m2, rotated into report axes and translated using the parallel
  axis theorem. Tensors are row-major about the COM or reference point as named.
- Weight vector, applied-load resultant, moments and the reaction wrench for a
  single ideal fixed support in static balance.
- Linear/angular momentum, kinetic and gravitational potential energy.
- The required resultant and additional applied wrench for prescribed linear
  and angular accelerations, including the gyroscopic term.

All assembly bodies are treated as one rigid aggregate for motion calculations.
Overlapping physical members/copies retain multiplicity; they are not silently
fused. This is instantaneous rigid-body balance, not a time-stepped dynamics,
contact, fatigue, deformation or finite-element solver.

```json
{
  "gravity":[0,0,-9.80665],
  "reference_point_mm":[0,0,0],
  "velocity_m_s":[1,0,0],
  "angular_velocity_rad_s":[0,0,2],
  "loads":[{"force_N":[0,0,100],"point_mm":[200,0,50]}]
}
```

An optional support `{origin_mm,normal,polygon_mm}` projects CG along gravity
onto a simple, planar declared support polygon. It reports containment and a
signed geometric stability margin. This check considers gravity only; other
loads do not constitute a certified tipping/contact analysis. Degenerate,
self-intersecting and nonplanar polygons reject.

Optional `axial_test:{material,force_N,area_mm2,length_mm}` calculates uniform
uniaxial stress, small elastic strain, extension and a yield ratio when declared
strength is available. It uses the caller's cross-section and excludes buckling.

`beam_test` adds two explicit prismatic elastic cases: `support: "cantilever_tip"`
or `"simply_supported_center"`, with `material`, `force_N`, `length_mm`,
`second_moment_mm4` and `extreme_fiber_mm`. It returns maximum moment, bending
stress, deflection and a declared yield ratio. Optional `buckling` requires
`minimum_second_moment_mm4`, `area_mm2`, `effective_length_factor` and
`compression_N`; it reports ideal Euler critical load and slenderness. The
weak-axis inertia and axial end condition are deliberately independent inputs.
Euler applicability is not inferred: yielding, imperfections, local buckling,
fatigue and combined bending/compression interaction remain unverified.

`weld_test:{effective_throat_mm,effective_length_mm,force_N,allowable_stress_Pa?}`
reports average direct-load stress and, with the caller's allowable stress,
utilization and required effective throat. It excludes eccentric moments,
weld-group load distribution, defects, fatigue and process qualification.
These two analytical cases work even when aggregate mass is unavailable.
Both return `geometry_linked:false` and `design_qualified:false`. They are
usable as explicit `doc_preview`/`doc_apply` postconditions; section properties,
load cases and material provenance remain the caller's responsibility.

The beam coefficients follow the [University of Illinois beam tables](https://courses.grainger.illinois.edu/tam251/fa2017/BeamDeflectionTables.pdf).
Ideal column limits follow [MIT's buckling lectures](https://ocw.mit.edu/courses/16-001-unified-engineering-materials-and-structures-fall-2021/mit16_001_f21_lec33lec34lec35.pdf).
Effective throat area is length times throat, as described by [AISC](https://ej.aisc.org/index.php/engj/article/view/628);
ARK does not supply a code-specific allowable stress or claim code compliance.

The mass-center and inertia conventions follow the
[NASA mass-distribution overview](https://nescacademy.nasa.gov/video/7f310373e3e64527810f8ae319db74b91d).
The uniform-gravity center is the mass-weighted component center, consistent
with [NASA's CG explanation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/aircraft-center-of-gravity/).

## Multicore parametric evaluation

The evaluator freezes authoring inputs and plans dependency keys/generations
before executing ready work. Audited native operations with explicit inputs can
run concurrently across independent branches, including ready descendants whose
parents have finished but have not yet been published. Results are published in
the original feature order. Failed dependencies never become committed edits.

Studio retains one session-execution thread between operations, so its owner
OCCT process remains reusable across live agent requests. The viewport pumps
messages and displays a stable frame while that thread owns the document.

Every task owns its mutable context and input copies. Native B-reps are processed
in isolated resident OCCT processes. Sketch, datum, instance and unknown implicit
dependencies retain conservative serial barriers. Lazy promotion requiring
additional legacy context stays on the owner. Parallel execution is therefore
bounded by known dependencies, not a promise that every feature can run at once.

The cache uses explicit parent generations for audited operations; other
features retain conservative upstream dependencies. An edit to one branch need
not invalidate independent branches. The task queue also caps estimated copied
input payload at 128 MiB; jobs that cannot fit run through the ordered owner.
This is a queued-input budget, not a cap on the final document's geometry.

Set these environment variables before starting Studio/MCP:

| Setting | Contract |
| --- | --- |
| `ARK_CPU_THREADS` | Overall CPU budget, capped by detected logical CPUs; default up to 32, explicit range 1..64. |
| `ARK_FEATURE_THREADS` | Maximum concurrent feature lanes, 1..16; default up to four. Small eligible sets remain serial. `1` is the feature-serial control. |
| `ARK_NATIVE_THREADS` | OCCT budget, further capped per active lane to avoid nested oversubscription. Serial barriers can use the available owner budget. |
| `ARK_NATIVE_ONESHOT=1` | Existing process-per-request transport control. |

`doc_info` and apply results expose feature_workers, native_threads_per_worker,
parallel_features, peak_parallel_features, dependency_edges and reuse counts.
`kernel_info` adds process-wide worker request/start counters alongside the
existing calling-thread counters. Nested CPU loops run serially within a worker;
external loops use bounded chunks, condition-variable waiting and exception
propagation. Transient legacy face-ID allocation is atomic; those IDs remain
unsuitable for persistent semantic references.

The first parallel evaluation pays for worker startup. Measure cold execution
separately from edits with resident workers, and compare geometry/topology as
well as timings. GPU rendering remains separate from authoritative CPU B-reps.

## Studio

The ribbon now has a Physics workspace, labeled icon tiles, contextual hover
help and unavailable-action states. Material editing adds declared elastic
properties. The load-case editor accepts world-coordinate gravity, reference point, velocities
and an applied force; selected-part queries transform those inputs into the part
frame while assembly queries use world coordinates. The physical drawer shows COM/CG, inertia and energy and
occludes underlying inspector text and hit targets. Load cases are inspection
settings for the Studio session, not saved simulation histories.

Two generated transparent atlases supply 32 icons to the OpenGL ribbon and
native command/context menus. [Assets and exact ImageGen prompts](../assets/ui/README.md)
are stored with the project and copied beside the executable by CMake.

`engineering-agents` tests rollback, prepared-candidate custody and zero-kernel
commit, finite-difference diagnostics, density units, elastic persistence, rigid
body equilibrium/inertia/energy, rotated tensors, frame member inertia, support
rejection and serial/parallel cold/warm parity. The Studio render self-test also
checks native menu icon bitmaps and captures the new physical drawer.

## Local validation, 2026-10-10

- Native CTest: 37/37 passed; core-only CTest: 25/25 passed.
- Studio render checks passed at 1600x1000 and 1180x800; native modeling,
  context menus, engineering modules and suspension UI checks passed.
- Live Studio/MCP prepared edit, hole update, mesh inspection, save, undo,
  New and disconnect checks passed. The render test verifies that committing a
  prepared shape updates the displayed mesh; the reference test verifies that
  a changed or tampered topology registry cannot reuse a stale cached frame.
- In three runs of an eight-branch box/transform/hole model (24 features), warm
  edits had a median 209.465 ms with one lane and 92.604 ms with four lanes:
  2.26x for this scenario. Cold medians were 230.141 ms serial and 273.787 ms
  parallel. Worker startup therefore outweighed parallel savings on this small
  cold workload. An isolated branch edit evaluated three features and reused 21.

These are local C++ measurements, not LLM latency/token measurements or a
general whole-model speed guarantee. [Raw evidence and source hashes](engineering-v2-validation.json)
record the controls. [Ribbon screenshot](engineering-v2-ribbon.png) and
[physical drawer screenshot](engineering-v2-physics.png) come from the real
Studio render test, not UI mockups.
