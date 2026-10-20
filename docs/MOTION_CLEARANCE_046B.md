# Motion clearance over baked motions — 046B

Milestones **C.04** and **D.04** remain **Parcial**. This adds a read-only
clearance check over a fresh [Skeleton Motion](SKELETON_MOTION_044B.md) bake:
exact B-rep distance at every baked frame, and a conservative certification of
the time between frames. It is ideal rigid kinematics. It produces no dynamics,
contact mechanics, deformation, tolerance-stack or manufacturing evidence.

## Request

`motion_info` gains a `clearance` object, valid only with `name`:

```json
{"name":"Window down and up","expected_revision":17,
 "clearance":{"pairs":[{"a":"Glass","b":"Beam"}],"threshold_mm":2,"max_subdivisions":4}}
```

| Field | Meaning |
| --- | --- |
| `pairs` | 1..256 `{a,b,copy_a?,copy_b?}`, node ID or unique occurrence name; at least one side must move |
| `scope` | without `pairs`: `all` (default) checks each moving occurrence against every other visible placed solid; `static` only against occurrences that never move |
| `threshold_mm` | required clearance, 0..1000, default 0 |
| `max_subdivisions` | bisection depth per frame interval, 0..10, default 4 |
| `from_frame`, `to_frame` | optional inclusive frame window; certification covers only that window |
| `expected_revision` | optional; a mismatch rejects with `REVISION_CONFLICT` |

A read-only tool argument was chosen over a new tool: clearance is derived
from one named bake and its digests, which `motion_info` already reads and
validates. Scope queries skip pairs whose relative pose the joint structure
fixes (one rigid cluster, or proven chains moved by the same joints) and
directly mated pairs, and report the counts in `pairs_skipped`; declare them
in `pairs` to check them. Every involved occurrence needs an exact native
solid; otherwise `MOTION_CLEARANCE_UNSUPPORTED_GEOMETRY`, never a mesh fallback.

## Binding and rejection

- A stale bake rejects with `MOTION_BAKE_STALE` and its reasons
  (`assembly_changed`, `graph_changed`, `graph_invalid`). Clearance never
  re-bakes. No bake: `MOTION_NOT_BAKED`.
- The frames are re-solved in double precision with the bake's solver path.
  The stored float32 frames must agree within 0.01 mm (rotation entries scaled
  by 1000 mm), else `MOTION_BAKE_MISMATCH`.
- The result carries `bound_to{revision,source_digest,graph_digest,bake_source_revision}`.
  It is derived evidence: nothing is saved and the document format is unchanged.
  A process cache of 8 results is keyed by motion name, both digests, fps, the
  request and the SHA-256 of every involved B-rep. A hit is returned only after
  the digests were verified at the current revision, with `cached:true`.

## Measurement per pose

The worker operation `collision_frames` reads both B-reps once and measures a
batch of poses with the existing collision contract: OCCT exact minimum
distance, and the exact boolean common within the fixed 1e-6 mm contact
tolerance (`interference` for positive common volume, else `touching`). The
host sends at most 128 poses per request and the worker stops after 8 s of
work; the host resumes from `completed`. The 30 s worker limit is unchanged and
still reports `RESOURCE_LIMIT`.

A box prefilter avoids exact work that cannot change the answer: the world AABB
of each body's native local bounds, enlarged by 1e-3 mm, encloses the body, so
the box gap `g` is a lower bound of the exact distance. A frame is measured
exactly when `g <= threshold + 1e-6 + d` for an adjacent interval. The exact
frame minimum is then completed by branch and bound over `g`.

## Certification between frames

Let `D(t)` be the exact distance between rigid bodies A and B. If every point
of A moves by at most `d` relative to B between time `s` and `t`, then
`D(t) >= D(s) - d` (distance is 1-Lipschitz in point displacement).

Each body gets a bounding sphere: centre `c` of its enlarged native local box,
radius `r` its half diagonal. For a frame interval `[a,b]`, `d` bounds the
displacement of any sphere point between either end and any time inside:

- **joint_chain (proven).** Rigid-side joints whose moving sets nest form a
  joint tree, so a body pose is a product of single-joint motions with
  parent-fixed axes. Telescoping that product gives, for each end `s`,
  `d_s = sum_j 2*(dist(c(s), axis_j(s)) + r)*sin(min(E_j,pi)/2)` for revolute
  joints and `sum_j E_j` (mm) for prismatic joints, over the joints that move
  only one body of the pair (common upstream joints cancel). `axis_j(s)` is the
  solved world joint axis at `s`; `E_j` is the joint excursion inside the
  interval measured from `s`. `d = max(d_a, d_b)`. As a consistency check the
  measured endpoint envelope below must not exceed `d`; otherwise the pair falls
  back to the envelope.
- **endpoint_envelope (assumed, never proven).** With `M = T_B^-1 T_A`,
  `d = |M(b)c - M(a)c| + r*||R(b) - R(a)||_2`, where the norm is
  `2 sin(theta/2)` for the relative rotation angle. It is used for closed
  loops, transmissions, constrained continuation, non-nesting joints and bodies
  that move without a recorded joint, and holds only if the relative motion
  stays inside the chord envelope of its two ends.

`E_j` comes from the analytic graph at the interval ends, every segment seam
on both sides and 8 interior points, plus half the larger neighbouring slope
times each sub-step and the joint solver tolerance (1e-4 deg, 1e-5 mm). This is
the only sampled ingredient of the bound.

With `V(s)` the exact distance or the box lower bound, an interval is
certified at target `T` when `min(V(a), V(b)) - d > T + 1e-6 mm`. `T` is
`threshold_mm` when both ends are at or above it, otherwise 0 (contact free).
Requiring both ends is conservative: one end suffices under the bound.
Otherwise the midpoint is solved by the same joint solver along the same frame
path (frame 0 through the merged times); baked frames reached by that solve
must reproduce the frame solve within 1e-5 mm, else `MOTION_CLEARANCE_REJECTED`.
Bisection stops at a measured contact or at `max_subdivisions`; what remains is
listed in `uncertified_intervals` with its reason and lower bound.

## Result

| Field | Meaning |
| --- | --- |
| `continuous_certified` | every interval in the window proven contact free, with proven bounds only |
| `threshold_certified` | every interval proven above `threshold_mm` |
| `envelope_assumption_certified` | present when some pair uses the envelope: certification under that assumption only |
| `continuous_lower_bound_mm` | smallest certified floor over all intervals; a proven floor, not an estimate of the minimum |
| `min_clearance_mm`, `worst` | smallest exact distance over the measured poses, with pair, time, nearest frame, `at_baked_frame`, status and nearest points |
| `min_clearance_exact_over_frames` | the frame minimum is exact (branch and bound completed) |
| `interference_frames`, `between_frame_contacts` | measured contacts at frames and at bisection poses, with common volume and overlap bounds |
| `uncertified_intervals` | intervals not proven contact free, with reason and lower bound |
| `markers` | runs of frame intervals: `interference`, `touching`, `uncertified`, `below_threshold`, `threshold_unproven` |
| `pairs` | up to 32 pairs by minimum: bound model, proof flag, minima, status |
| `evaluations`, `timing_ms`, `method` | counts, timings and the exact method strings |

Penetration depth is not computed: interference reports the common volume and
the overlap's bounding box only.

## Capacity and cancellation

At most 1024 candidate pairs, 1,048,576 pair-frames, 4096 exact pose
evaluations (frames and bisection poses) and 2048 bisection poses per query.
Exceeding the frame-level exact budget rejects with `MOTION_CLEARANCE_CAPACITY`
(`RESOURCE_LIMIT`): declare fewer pairs or split with `from_frame`/`to_frame`.
Exhausted bisection budgets leave intervals uncertified with an explicit
reason. `inspectMotionClearance(document,revision,name,request,cancel)` polls
`cancel` before the solve, between native requests and between bisection
levels (`MOTION_CLEARANCE_CANCELLED`, no result). It temporarily drives the
document's assembly placements and restores them, and may materialize native
bodies; run it on the session document or a snapshot, not concurrently with
edits.

## Studio

The Baked Motions panel title line has **Check clearance** and a threshold chip
(0, 0.5, 1, 2, 5 mm). The result shows the minimum and pair (click scrubs to
the worst frame; between-frame results show the nearest baked frame) and the
certification state. The vector view and the timeline show red interference,
amber below-threshold, hatched amber threshold-unproven and hatched grey
uncertified intervals, plus a tick at the worst time. Results are shown only
for the motion, revision and document they were measured on; a stale bake
shows the explicit rejection. The panel calls the query through one function,
`runMotionClearance046B(motion, threshold_mm, done)`, the integration point for
the shared off-UI-thread exact-query runner; it runs synchronously today.

## Local evidence

Windows MSVC Release against the patched OCCT SDK, synthetic fixtures only.
`tests/motion_clearance_046b_tests.cpp` (CTest `motion-clearance-046b`):

- A revolute arm (x 20..120 mm, 2 mm wide) sweeping 0..90 degrees at 10 fps.
  A block 2 mm above its swept band: minimum 2.000 mm at the 45 degree frame,
  certified after bisection. A post whose analytic minimum,
  `125 - sqrt(120^2+2^2) = 4.98334 mm`, lies between frames 8 and 9: frames
  alone give 5.0183 mm; bisection measured 4.98493 mm; the certified floor
  stayed below the analytic minimum.
- A stop whose face starts at `60 - asin(2/30) = 56.18` degrees: interference
  exactly at frames 7..10; frame 6 at `30 sin 6 - 2 = 1.13585 mm`.
- A 0.4 mm blade between the 36 and 45 degree frames: no frame touches it
  (closest 5.0618 mm); with `max_subdivisions:0` the interval is flagged
  uncertified; with bisection the 40.5 degree pose finds the interference.
- Stale rejection after a placement and a graph change, no re-bake; revision
  and digest binding; cache hit; a B-rep edit that keeps the bake fresh misses
  the cache and measures the new 1 mm gap; `REVISION_CONFLICT`; pair filtering
  and validation; cancellation; capacity as `RESOURCE_LIMIT` and a frame window.
- A coupled three-joint serial arm keeps the proven bound. Against a near wall,
  481 exact poses at 120 Hz through the same solver never fell below the
  certified floor. A closed four-bar uses the envelope and is never
  `continuous_certified`.

Studio `studio-baked-motions-ui` and `-compact` (1440x900, 1366x800) cover the
action, threshold, markers, worst-frame scrub, read-only behaviour and stale
rejection; captures were inspected. Timings and counts are in the 046B
verification record; they are local measurements, not performance claims.

## Not established

- Dynamics, inertia, forces, contact response, elastic deformation, clearance
  under tolerances, or manufacturing qualification.
- A proof for closed loops, transmissions or constrained continuation (envelope
  assumed), or for joint excursions between the sampled graph points of
  function nodes and couplings that change faster than those samples.
- Penetration depth; hidden occurrences in scope queries; mesh-only bodies.
- Production-scale performance and the asynchronous Studio integration.
