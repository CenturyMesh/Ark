# Skeleton Motion and Baked Motions — 044B

Milestones **B.04**, **C.04**, **D.04** and **G.05** remain **Parcial**. This
work adds an authored motion layer over existing assembly joints, a solver bake
and a Studio player. It is ideal kinematics: no dynamics, inertia, force,
contact or continuous collision evidence is produced or implied. The separate
parametric [Skeleton code editor](SKELETON_AUTHORING.md) is unchanged; the
"skeleton" here is the set of driven assembly joints.

## Model

The skeleton is every **revolute** (degrees) or **prismatic** (millimetres)
mate whose moving side reaches a grounded support, exactly as used by
[joint-aware manipulation](ASSEMBLY_MANIPULATION.md). The coordinate is B
relative to A. Driven followers need no channel of their own: rigid
attachments and closed loops follow through the same solver, as do patterned
followers and the chain/gear/shaft/rack/cam couplings of
[mechanical motion](MECHANICAL_MOTION.md). The 044B fixtures verify rigid
attachments, a serial arm and a closed four-bar; patterned followers and
transmissions reuse the existing drive contracts without new 044B fixtures.

A **motion** is a named, persisted vector graph. Each channel binds one joint
coordinate and describes its value over time with analytic and cubic Bezier
segments combined by graph nodes. There are no keyframe lists; agents author
curves and composition, then ask ARK to bake them.

```json
{"op":"motion.graph.set","name":"Window down and up","graph":{"channels":[
  {"name":"glass","mate":1,"from":0,"curve":{"type":"sequence","items":[
    {"type":"ease","duration":2,"to":"-glass_travel"},
    {"type":"hold","duration":0.5},
    {"type":"ease","duration":2,"to":0}]}}]}}
```

```json
{"op":"motion.graph.set","name":"Robot wave","graph":{"channels":[
  {"name":"shoulder","mate":1,"from":0,"curve":{"type":"sequence","items":[
    {"type":"ease","duration":1,"to":30},
    {"type":"sine","duration":2,"amplitude":15,"period_s":1},
    {"type":"ease","duration":1,"to":0}]}},
  {"name":"elbow","mate":2,"expr":"0.5*shoulder+10"},
  {"name":"wrist","mate":3,"from":0,"curve":{"type":"ping_pong","count":2,
    "item":{"type":"bezier","duration":1,"to":40,"c1":[0.3,0],"c2":[0.7,40]}}}]}}
```

`graph` fields: `channels` (1..32), optional `duration_s` (defaults to the
longest curve; shorter channels hold their end value), `allow_jumps` (default
false) and `description`. A channel has an identifier `name`, an optional
numeric `mate`, and exactly one of `curve` or `expr`. A curve channel without
`from` starts at the joint's current measured coordinate. A channel without
`mate` is a signal used only by couplings. Numeric fields accept numbers or
document parameter expressions.

| Node | Fields | Value |
| --- | --- | --- |
| `hold` | `duration`, `value?` | constant |
| `linear` | `duration`, `to` | straight line from the start value |
| `ease` | `duration`, `to`, `ease: in, out, in_out` | cubic ease (`in_out` is C1 with zero end velocity) |
| `bezier` | `duration`, `to`, `c1:[t,v]`, `c2:[t,v]` | cubic Bezier in (time, value); control times strictly inside (0, duration) |
| `sine` | `duration`, `amplitude`, `period_s`, `center?`, `phase_deg?` | `center + amplitude*sin(2*pi*t/period + phase)` |
| `spring` | `duration`, `to`, `frequency_hz`, `damping?` | damped step response from rest |
| `function` | `duration`, `expr` | expression in `t`, `u=t/duration`, `from` and parameters |
| `sequence` | `items` | plays in order; each item starts at the previous end value |
| `parallel` | `items`, `combine: add, min, max` | layers items; with `add`, later items start from 0 |
| `delay` | `seconds`, `item` | holds the item's start value, then plays it |
| `repeat` | `count`, `item` | repeats the item |
| `ping_pong` | `count?`, `item` | forward then time-mirrored back |
| `reverse` | `item` | time reversal |
| `time_scale` | `item`, `factor` or `duration` | retimes the item |
| `offset` | `item`, `value?`, `gain?` | `gain*item + value` |

Coupled channels use the existing expression evaluator over other channel
names, `t` and parameters. Velocities follow the chain rule numerically.
Nesting is bounded to 16 levels and 512 nodes per graph.

## Validation

`motion.graph.set`, `motion.bake` and `motion_info{graph}` reject explicitly and
atomically:

| Code | Cause |
| --- | --- |
| `MOTION_UNKNOWN_JOINT` | the mate does not exist |
| `MOTION_UNSUPPORTED_JOINT` | the mate is not revolute or prismatic |
| `MOTION_DUPLICATE_JOINT` | two channels bind one mate |
| `MOTION_JOINT_NOT_DRIVABLE` | no grounded moving side, for example both sides grounded |
| `MOTION_LIMIT_VIOLATION` | a sampled value leaves the evaluated joint limits |
| `MOTION_DISCONTINUITY` | a value jump between segments without `allow_jumps:true` |
| `MOTION_COUPLING_CYCLE` | coupling expressions depend on each other cyclically |
| `MOTION_INVALID_EXPRESSION`, `MOTION_NONFINITE` | an expression fails or a value is not finite |
| `MOTION_BEZIER_NOT_MONOTONIC` | a control time is on or outside the segment boundary |
| `MOTION_FRAME_STEP_TOO_LARGE` | an unlimited revolute turns 180 degrees or more between frames |
| `MOTION_CHANNEL_CONFLICT` | after a frame solve a channel's joint does not hold its target |
| `MOTION_BAKE_REJECTED` | the joint solver refused a frame; frame, time, channel and solver code are included |
| `MOTION_BAKE_CAPACITY` | more than 16384 moving instance-frames, 7201 frames or 2 MiB of frames per document |

The graph limit check samples each channel at 240 Hz plus every segment
boundary. Each baked frame is then checked again by the joint solver, with the
existing limit roundoff and residual tolerances. Continuity is reported per
channel as `C1`, `C0` (velocity break) or `discontinuous`, with every break's
time, value jump and slope jump and a separate `loop_continuity` for
end-to-start playback. Velocity breaks are reported, not rejected.

## Bake

```json
{"op":"motion.bake","name":"Window down and up","fps":30}
```

`fps` is an integer 1..240 (default 30). Frames are `k/fps` up to the duration,
with a final frame exactly at the duration. For each frame ARK evaluates the
graph and drives every bound joint, in channel order, with
`previewAssemblyManipulation` from the previous frame's pose: rigid sides move
exactly, closed loops and transmissions use the existing constrained
continuation. After each frame it rebuilds the static solve and verifies every
channel within 1e-5 mm or 1e-4 degrees. The work placements are restored
afterwards; authored placements never change.

The bake stores only instances that move:

```json
"bake":{"format":"ark.baked_motion.v1","fps":30,"frame_count":136,"duration_s":4.5,
  "source_revision":12,"source_digest":"<sha256>","graph_digest":"<sha256>",
  "instances":[{"node":2,"copy":0,"name":"Glass"},{"node":3,"copy":0,"name":"Carriage"}],
  "channels":[{"name":"glass","mate":1,"unit":"mm","start":0,"graph_start":0,"min":-420,"max":0}],
  "frames_f32le_base64":"...","coordinates_f32le_base64":"...",
  "solver":{"rigid_side_steps":120,"constrained_steps":0,"max_continuation_steps":0,"max_channel_error":0},
  "clearance_checked":false,"dynamics_checked":false}
```

Frames are little-endian float32, frame-major and instance-minor, 12 values per
instance: the column-major rotation columns followed by the translation (mm).
They are display poses. Coordinates are the measured joint values per frame.
`source_digest` hashes assembly nodes, mates, parameter values and the solved
placements at the bake. A later change to any of them reports the motion
**stale** with `assembly_changed`; replacing the graph reports `graph_changed`.
A stale bake stays playable and flagged until `motion.bake` runs again.

`motion.rename{name,new_name}` and `motion.remove{name}` complete the commands.
Every motion command requires `expected_revision` through `doc_apply` or
`doc_preview`, is part of one atomic batch, and is undone and redone with the
persistent iteration history.

## Inspection

`motion_info` is read-only and never changes revision or placements:

- `{}` lists motions with duration, fps, frame count and stale reasons.
- `{name}` returns the graph, joint bindings, continuity, sampled ranges and the
  bake summary without frame payloads; `{graph}` validates an uncommitted graph.
- `{name|graph, time_s}` evaluates channel values and velocities;
  `solve:true` also drives the joints from the committed pose through every
  frame up to `time_s` (at most 600 frames) and returns solved instance matrices.
- `{name, frame}` decodes one baked frame.

Load `agent_onboard{topic:"motion"}` for the `skeleton_motion` guide; Studio
agents receive `motion_info` with that topic.

## Studio

**Model > Motions** (or the tool gallery's Motion group) docks the **Baked
Motions** panel to the right of the viewport, which shrinks to make room. A
successful `motion.bake`, from an agent or from the panel, opens the panel on
that motion without starting playback. The panel lists motions with duration,
channel count, fps and a `BAKED`, `STALE`, `NOT BAKED` or `INVALID` badge. The
selected motion shows a vector view of every channel, with Bezier control
handles, C0/C1 break markers and the playhead; channels with the same unit share
one labelled vertical scale. A legend shows the current joint values and
continuity, above a timeline scrubber.

Play/Pause, Stop, Loop and speed (0.25x to 2x) replace display transforms with
baked frames only. Playback shows the nearest baked frame, never an
interpolated pose; bake at a higher fps for smoother display. Stop, Escape,
closing the panel, a component move or mechanical motion restore the solved
pose. Space toggles playback, Left/Right step a frame, Home/End seek. Rename,
Bake and Delete use the ordinary revision-checked `doc_apply`. Rename uses the
accessible native editor, and Delete can be undone.

## Persistence

Documents with motions conditionally require document source version **47**.
Older readers reject them instead of dropping the motions. Removing every motion
returns the document to its previous required version. Application identity is
independent of this format.

## Limits and scope

- Only revolute and prismatic coordinates are channels; concentric/coincident
  joints and patterned drivers are not driven directly.
- Joint limits are checked on sampled graph values and on every baked frame,
  not continuously between frames.
- Collision and clearance are not checked by the bake (`clearance_checked:
  false`). Check sampled poses with `assembly_info{sweep}` or
  `collision_inspect`.
- The bake drives channels in declaration order from the previous frame. A
  closed loop driven by two independent channels rejects instead of choosing
  one.
- Float32 frames are display evidence; the authored graph, joints and
  placements remain the geometric authority.

## Verification record

See the 044B iteration record for the integrated release. Local evidence for
this module, Windows MSVC Release build against the patched OCCT SDK:

- `tests/motion_graph_044b_tests.cpp` (CTest `motion-graph-044b`) passed
  **351 checks** using only synthetic fixtures: segment and Bezier timing,
  composition nodes, couplings and their velocities, continuity diagnostics,
  the rejections above, a glass pane on a prismatic rail with limits
  [-420, 0] mm baked down and up (136 frames at 30 fps, only the glass and its
  rigid carriage stored), a three-joint revolute arm wave checked against serial
  forward kinematics, a parallelogram four-bar baked through constrained
  continuation, a two-channel loop conflict, stale detection after parameter,
  placement, mate-limit and graph changes, JSON and binary `.ark` round trips,
  the version 46 rejection, undo/redo and `doc_preview`/stale
  `expected_revision` contracts. A timing probe with 123 occurrences and 32
  mates measured 79 to 96 ms for `motion.graph.set` plus the 136-frame bake,
  including `doc_apply` validation. This is one local measurement, not a
  performance claim for production assemblies.
- `studio-baked-motions-ui` and `studio-baked-motions-ui-compact` (1440x900 and
  1366x800) passed: an agent bake opens the panel, Model > Motions hides and
  shows it, play, pause, Space, scrub, frame step, loop, speed, rename, stale
  badge, bake, delete/undo, the unbaked and empty states, controls inside the
  dock and the viewport display-style menu kept outside it. The captures were
  inspected for clipped text and overlaps.
- The complete local CTest run passed 197 of 199. `studio-design-container-ui`
  and `studio-assistant-ui` failed only from the long worktree build path:
  their staging files exceed the Windows 260-character path limit. Both passed
  when the same executable wrote to a short temporary directory.
- A GCC 16 (MinGW) `-fsyntax-only` check of the new and changed portable
  sources found no errors. The Ubuntu and Windows portable CI builds were not
  run locally.

No private user design is a fixture or a distributed artifact.
