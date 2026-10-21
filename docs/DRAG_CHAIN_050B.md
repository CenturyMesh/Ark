# Native drag chain — 050B

Scope: **B.04** (assemblies) and **C.04** (motion), both remain **Parcial**.
A cable carrier (drag chain, energy chain) that follows a moving carriage is a
native, reusable recipe. Its links are real B-rep occurrences whose poses are
derived from the carriage coordinate at every assembly state. It replaces the
pattern of rigid telescoping segments with `min()` couplings, which does not
behave like a chain. The model is ideal rigid-link kinematics. It does not
establish sag under self-weight, dynamics, cable fill, fatigue or supplier
qualification of a particular chain series.

## Authoring contract

`doc_apply` (and `doc_preview`) with the observed `expected_revision`; atomic
and undoable like every other command:

```json
{"op":"drag_chain.set","name":"Camera cable chain",
 "link":{"pitch":"chain_pitch","outer_width":31.6,"outer_height":21.6,"inner_width":25,"inner_height":17},
 "bend_radius":"chain_radius",
 "anchor":{"occurrence":"Duct","origin":[60,-20,405],"direction":"-Z","bend":"+X"},
 "moving":{"occurrence":"Carriage","attach":[-24,20,-8],"joint":1},
 "link_count":25,"margin":30,"travel":{"min":"lift_min","max":"lift_max"},"max_slack":120}
```

`drag_chain.remove{name}` deletes the record, its group, its link occurrences
and the link definition when nothing else uses it.

- `link`: pitch (pin to pin), outer width (across the bend axis), outer height
  (radial, in the bend plane) and the inner cable space. Inner sizes default to
  80 % of the outer ones. Numbers or parameter expressions.
- `bend_radius` R: radius of the pin line in the U bend.
- `anchor`: the fixed-end pin. `origin` is in assembly space, or in the frame
  of `anchor.occurrence` (a part occurrence). `direction` (`+X..-Z` or
  `[x,y,z]`) runs from the fixed bracket towards the bend; `bend` points from
  the fixed run to the moving run and must be perpendicular to `direction`.
- `moving`: the occurrence the moving run attaches to (the carriage), the
  moving-end pin `attach` in its frame (default origin) and `joint`, the
  prismatic mate whose coordinate is q.
- `link_count` optional 1..400. Without it the count is derived as
  `ceil((pi*R + max|c| + margin)/pitch)`, `margin` defaulting to two pitches.
- `travel{min,max}` optional, in q (mm); otherwise the joint limits.
- `max_slack` optional, default `max(4*pitch, pi*R)`.

Occurrence names bind to IDs when the command is applied. Replacing a chain
keeps its definition, group and leading link IDs; extra links are removed from
the tail and new ones appended. Up to 16 chains per document.

### Typed rejections

| Code | Cause |
| --- | --- |
| `DRAG_CHAIN_NOT_PARALLEL` | The moving-end pin is not at 2R along `bend` with zero lateral offset (1e-3 mm), at the current pose or a travel end; `bend` not perpendicular to `direction` |
| `DRAG_CHAIN_CARRIAGE_NOT_DRIVEN` | The pin does not move 1 mm along `direction` per mm of q (1e-6), e.g. the occurrence is on the grounded side or the joint axis is not parallel to the runs |
| `DRAG_CHAIN_TRAVEL_RUN_NEGATIVE` | An explicit link count would make the fixed or moving run negative at a travel end |
| `DRAG_CHAIN_TRAVEL_EXCLUDES_POSE` | The current q lies outside the declared travel |
| `DRAG_CHAIN_TOO_SHORT` | The links cannot pass the bend, or a run is negative at the current pose |
| `DRAG_CHAIN_TOO_LONG` | An explicit count exceeds the travel requirement by more than `max_slack` (the loop would overhang the stroke) |
| `DRAG_CHAIN_UNKNOWN_OCCURRENCE` / `DRAG_CHAIN_UNKNOWN_MATE` | Missing, ambiguous or group endpoint; missing joint |
| `DRAG_CHAIN_JOINT_NOT_PRISMATIC` / `DRAG_CHAIN_JOINT_NOT_DRIVABLE` | q must be a prismatic coordinate with a grounded moving side; a probe outside the limits |
| `DRAG_CHAIN_BEND_TOO_TIGHT` | R below the pitch, or adjacent links would collide in the bend (below) |
| `DRAG_CHAIN_GEOMETRY` | Inner space not inside the outer section, walls or crossbars below 0.25 mm, sizes above the caps |
| `DRAG_CHAIN_NONFINITE` | A value or expression is not finite or exceeds 1e6 |
| `DRAG_CHAIN_CAPACITY` | Outside 1..400 links, more than 16 chains or 4096 assembly nodes |
| `DRAG_CHAIN_LINK_DRIVEN` | A link occurrence is mated, placed, fixed, patterned or reparented |
| `DRAG_CHAIN_NOT_FOUND` / `DRAG_CHAIN_INVALID` | Unknown chain; malformed or unknown field |

After authoring, every resolve re-checks the chain. A parameter edit, drive or
bake frame that breaks the 2R offset or makes a run negative rejects the whole
transaction with `DRAG_CHAIN_NOT_PARALLEL` or `DRAG_CHAIN_RUN_NEGATIVE`.

## Kinematics

Let A be the fixed pin, d the run direction, n the bend side, N links of
pitch p, L = N·p, and c = (C − A)·d for the moving pin C. The path from the
fixed pin is the fixed straight of length L_f, a semicircle of radius R centred
at A + L_f·d + R·n, and the moving straight back along −d to C = A + 2R·n + c·d.

**Continuous (arc-length) model.** L_f + πR + L_m = L and L_f − L_m = c, so

    L_f = (L − πR + c)/2,   L_m = (L − πR − c)/2,   dL_f/dc = 1/2.

Both runs are nonnegative while |c| ≤ L − πR. This is the user's formula with
c = q_attach(q) − anchor along the run axis.

**Rigid links (what ARK places).** A link cannot follow the arc length exactly:
pins on a circle one pitch apart subtend α = 2·asin(p/2R), not p/R. ARK places
pin 0 at A, pins on the fixed straight at A + i·p·d, pins on the circle at
consecutive chords of exactly p (including both transition links), and pins on
the moving straight at C + (N − i)·p·d. L_f is the unique root of the monotonic
pin walk that ends at C (bisection to round-off). Consequences, all checked:

- fixed-run links are bitwise identical for every c; moving-run links translate
  with the carriage by Δc·d and keep their orientation;
- consecutive pins are exactly p apart (observed max error 1.3e-13 mm) and lie
  on the path (3.6e-14 mm); pin arc lengths are strictly increasing;
- L_f(c + 2p) = L_f(c) + p exactly, so the loop moves at exactly half the
  carriage speed per two pitches, with a bounded polygon ripple. For p = 15,
  R = 38 the rigid L_f exceeds the continuous formula by 0.371–0.395 mm because
  chords consume less chain than the arc (πR·(1 − p²/24R²) to first order);
- link frames: origin at the rear pin, X towards the front pin (the chord, i.e.
  tangent to the path at the chord midpoint), Y = bend × direction, Z towards the
  loop centre; orientation is continuous in c (observed below 1.4e-3 rad per 0.1 mm).

The loop apex is A + (L_f + R)·d + R·n.

## Geometry

One `native_solid` definition with `native.op:"drag_chain_link"` and the five
section expressions on `p`. The OCCT worker fuses boxes and cylinders into one
validated solid in link coordinates (X pitch, Y width, Z height, centred on the
pin line). Each side wall has two plate layers of t = (W − Wi)/4: the rear lobe
(radius min(0.28p, 0.45H)) and neck use the outer layer, the front lobe and
neck the inner layer with 0.1 mm running clearance, so each link's rear lobe
nests outside its predecessor's front lobe. A full-height centre plate spans
both layers over 0.4p and carries the top and bottom crossbars around the
Wi × Hi cable space. Pins and stop dogs are not modeled.

Adjacent centre plates clear while (H/2)·tan(α/2) < 0.29p and the inner side of
the bend stays open (R·cos(α/2) > H/2); `drag_chain.set` rejects otherwise with
the minimum R. The fixture link (15 pitch, 31.6 × 21.6, 25 × 17 inside) has a
volume of 1964.6 mm³.

The N link occurrences live under one group named after the chain. They are
ordinary instances of the definition: BOM quantity, material and appearance of
the definition (dark grey by default), visibility of the group or each link,
picking, `collision_inspect` and motion clearance use the real solid.

## Driven followers

`resolveAssembly` compiles the chains (`assemblyFollowers`) and, after the mate
solve, re-derives each link's copy-0 world frame from the solved anchor and
carriage frames. Links are therefore never solver variables, are not reported
as unconstrained instances and cannot be mated or placed.
`buildAssemblyManipulation` keeps the same follower in the program;
`previewAssemblyManipulation` re-poses links from the previewed frames and never
emits pose commands for them. Static pose, `assembly_info` (including
`drive` previews), committed `assembly.drive`, Studio dragging and every motion
bake frame (which resolves through the same manipulation program) therefore
show the links of that frame's q. No motion-graph channel per link exists;
links whose pose changes appear in the bake
as moving instances; links that stay on the fixed run are omitted like any
static occurrence.

The motion source digest includes the chain recipe, so recipe edits invalidate
previous bakes even when the current pose is unchanged. Documents without chains
retain their previous digest. Bakes use the 049A derived-frame receipts.
Motion clearance measures exact solids at sampled frames, but driven links do
not have a proven rigid joint-tree displacement bound: intervals involving them
remain uncertified (`continuous_certified:false`).

## Agent surface

`drag_chain_info{name?,q?,poses?,expected_revision?}` is read-only
(`readOnlyHint`, batchable in `query_batch`). No arguments lists chains with
q, pitch, R and both runs. `name` returns the section, anchor and moving end,
q, the state (carriage coordinate, fixed and moving runs, loop tangent and
apex, the continuous formula and polygon offset, segment counts, pin checks, a
link summary or every pose with `poses:true`), travel ends with their runs,
the length budget (chain, bend, required, margin, slack, max slack) and
warnings. `q` evaluates a hypothetical coordinate through the assembly.drive
preview solver without changing the document. `agent_onboard` topics
`assembly` and `motion` carry a `drag_chain` section with the rules, the
example and the instruction never to model a chain as telescoping segments.

## Studio

Links render, pick and play like any instance; Baked Motions playback shows
them rolling through the bend. Selecting a link shows `DRAG CHAIN / <name>`
in Properties (link index, pitch and R, carriage coordinate, both runs, loop
apex and the driving joint), solved from the displayed frames so it follows
playback. The panel exposes the shared link definition's section through
`Edit link geometry` and its driving expressions through `Source parameters`.
Individual link placement, grounding and deletion controls are omitted because
the chain owns those occurrences.
No new action IDs are used.

## Persistence

Documents store `drag_chains` as an optional top-level array, like 048B
`exploded_views`, without a document format change. Readers validate its
structure (`DRAG_CHAIN_*` on load); references are re-checked by every
resolve. Undo, redo, iteration history and save/reopen use the ordinary
authoring snapshot. Builds before 050B ignore the array and cannot evaluate
the `drag_chain_link` definition (explicit native-op error). Documents without
chains are unchanged.

## Verification record

`tests/drag_chain_050b_tests.cpp` (`drag-chain-050b`): a 4501-sample carriage
sweep of the pure kinematics (pin spacing and path, fixed and moving
invariants, 2-pitch periodicity, half speed, continuity, ripple bound, typed
kinematic and section rejections), then, with the native worker, a synthetic
kiosk lift: vertical prismatic carriage q ∈ [120,570] mm (joint limits), R 38,
pitch 15, 31.6 × 21.6 mm links, fixed pin at mid travel inside a duct. It
checks 18 typed rejections with unchanged document and revision, the derived
25 links, the native link B-rep, static and committed-drive poses against the
analytic layout, an `assembly_info` drive preview without link pose commands,
exact clearance (adjacent bend links separated by 0.1 mm, next-but-one links
and the 54.4 mm gap between runs), BOM, `drag_chain_info` (list, detail,
q = 120, poses, rejections), a carriage bake (81 frames at 20 fps: the first
link never moves, baked link poses match the analytic layout at each frame's q
within 3.4e-5 mm of float32 rounding, loop at half speed within 2.2e-4), mate,
placement and removal rejections on links, persistence round trip and a
duplicate-link load rejection, replacement with stable IDs, undo/redo, remove
and onboarding/tool registration.

`--self-test-drag-chain-ui` builds the same chain in Studio, bakes the carriage
at 30 fps, captures q = 120, 345 and 570 and a selected bend link's Properties,
and checks the static fixed run, the rigid carriage end and the half-speed loop.
Local results and integration limits are listed in the
[050B delivery notes](iterations/050B.md).
