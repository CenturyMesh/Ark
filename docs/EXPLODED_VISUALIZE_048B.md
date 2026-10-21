# Exploded Views and Visualize — 048B

Milestones **B.04**, **B.05** and **D.04** remain **Parcial**. This work adds
named exploded views over assembly instances, a link from the
[Skeleton Motion](SKELETON_MOTION_044B.md) graph to an exploded view factor, and
Visualize: on-the-fly assembly images with BOM balloons and a BOM table. Exploded
positions are display transforms only. They never change B-rep, mates, authored
placements, mass properties or clearance, and B-rep remains the geometric
authority. Images are visualizations of display tessellation.

## Model

An exploded view is a named, persisted list of ordered steps. Each step moves a
set of assembly instances along a world direction by a distance.

```json
{"op":"explode.set","name":"Service","description":"Lid, screws then cartridge","steps":[
  {"instances":["Cover"],"direction":"+Z","distance":"lid_lift","label":"Cover"},
  {"instances":["Screw A","Screw B"],"direction":"+Z","distance":40,"with_previous":true},
  {"instances":["Cartridge"],"direction":[1,0,0],"distance":120}]}
```

| Field | Meaning |
| --- | --- |
| `instances` | 1..256 assembly instance IDs or unique names; names are stored as IDs |
| `direction` | `+X`, `-X`, `+Y`, `-Y`, `+Z`, `-Z` (case-insensitive, sign optional) or a nonzero finite `[x,y,z]`, normalized |
| `distance` | millimetres, a number or a document parameter expression, finite within 1e6 mm; negative moves against the direction |
| `with_previous` | optional; the step shares the previous step's slice (not allowed on the first step) |
| `label` | optional text, at most 128 characters |

`explode.set` creates or replaces a view; `replace:false` rejects an existing
name. `explode.rename{name,new_name}` and `explode.remove{name}` complete the
commands. A document holds at most 16 views of at most 64 steps.

**Factor.** A factor t in [0,1] plays the steps in order. Each step without
`with_previous` opens a new slice; with S slices, slice k covers
[k/S, (k+1)/S] and a step's progress is `clamp(t*S - k, 0, 1)`, linear inside
its slice. Its offset is `direction * distance * progress`. A step naming a
group moves every descendant occurrence, a pattern instance moves all its
copies, and offsets of several steps accumulate. For the example above, S = 2:
at t = 0.25 the cover and both screws are half way, the cartridge waits; at
t = 0.75 the cartridge is half way. The display transform of an occurrence is
`translation(offset) * solved_world`. Translation only: rotations are not part
of 048B.

The distance expressions are evaluated at use. An instance removed after
authoring stays in the view, is reported as missing and contributes no offset;
assembly IDs are never reused.

## Validation

All explode commands require `expected_revision` through `doc_apply` or
`doc_preview`, are part of one atomic batch, and are undone and redone with
the persistent iteration history. Rejections are explicit, leave the document
unchanged and expose the code as `error_code`:

| Code | Cause |
| --- | --- |
| `EXPLODE_UNKNOWN_INSTANCE` | an instance ID or name does not exist |
| `EXPLODE_AMBIGUOUS_INSTANCE` | several instances share the name; use the ID |
| `EXPLODE_DUPLICATE_INSTANCE` | one step lists an instance twice |
| `EXPLODE_ZERO_DIRECTION` | the direction vector has zero length |
| `EXPLODE_NONFINITE_DISTANCE` | the distance is not finite or exceeds 1e6 mm |
| `EXPLODE_INVALID_EXPRESSION` | the distance expression does not evaluate |
| `EXPLODE_STEP_CAPACITY` | more than 64 steps, or more than 256 instances in a step |
| `EXPLODE_CAPACITY` | a 17th view |
| `EXPLODE_NAME_CONFLICT` | `replace:false` on an existing name, or a rename onto one |
| `EXPLODE_NOT_FOUND` | rename or remove of a missing view |
| `EXPLODE_VIEW_IN_USE` | rename or remove while a motion channel drives the view |
| `EXPLODE_INVALID` | unknown fields, malformed values, `with_previous` on the first step |

## Link to the animation skeleton

A motion graph channel without `mate` may declare `explode_view`; its value is
that view's factor. It is an ordinary signal channel: any curve node or
coupling expression may drive it, and it can run beside joint channels.

```json
{"op":"motion.graph.set","name":"Explode and open","graph":{"channels":[
  {"name":"explode","explode_view":"Service","from":0,"curve":{"type":"sequence","items":[
    {"type":"ease","duration":2,"to":1},
    {"type":"hold","duration":1},
    {"type":"ease","duration":2,"to":0}]}},
  {"name":"door","mate":2,"from":0,"curve":{"type":"ease","duration":5,"to":90}}]}}
```

```json
{"op":"motion.bake","name":"Explode and open","fps":30}
```

| Code | Cause |
| --- | --- |
| `MOTION_UNKNOWN_EXPLODE_VIEW` | the named exploded view does not exist |
| `MOTION_DUPLICATE_EXPLODE_VIEW` | two channels drive one view |
| `MOTION_EXPLODE_RANGE` | a sampled value (240 Hz plus every segment boundary) leaves [0,1] |
| `MOTION_INVALID_GRAPH` | the channel sets both `mate` and `explode_view` |

The bake stores the factor per frame in the existing channel coordinates and
names the binding in the bake channel row; the exploded offsets are not baked
and stay display transforms:

```json
"channels":[{"name":"explode","explode_view":"Service","unit":"factor","start":0,"graph_start":0},
            {"name":"door","mate":2,"unit":"deg","start":0,"graph_start":0,"min":0,"max":120}]
```

`BakedMotionFrames::explodeViews` carries the binding to Studio. During Baked
Motions playback each frame first takes the baked joint poses, then applies the
view's offsets at that frame's factor. A motion with only explode channels bakes
with no moving joint instances. The view's steps are evaluated at playback, so
editing the view changes the animation without a new bake; the bake's
`source_digest` does not include exploded views. `motion_info` reports these
channels with `unit:"factor"` and `explode_view`.

## Inspection

`explode_info` is read-only and never changes revision or placements:

- `{}` lists views with step and slice counts, missing instances and the
  motions that drive them.
- `{name}` returns the authored view and resolved steps: instance names, unit
  `direction`, evaluated `distance_mm`, `slice` and `factor_range`.
- `{name, factor}` adds `state.moved_occurrences[{node,copy,name,offset_mm}]`
  and per-step progress.
- `{propose:{axis?,scope?,gap_mm?,instances?,name?}}` returns an uncommitted
  `explode.set` built from display bounding boxes: grounded units (or the largest
  unit) stay, every other unit moves along the axis it sits furthest out on
  (`axis:auto`) or along `±axis`, outermost first, clearing the units inside it
  by `gap_mm` (default a quarter of the median unit extent). `scope:top` treats
  each top-level group as one unit. Review it with the user before committing.

## Visualize

`visualize_render` is a read-only software render returning an inline PNG
through the same image envelope as `snapshot`.

```json
{"expected_revision":12,"exploded_view":"Service","explode_factor":1,
 "balloons":true,"bom_table":true,"lighting":"technical","background":"white",
 "resolution":"1920x1080","samples":9,"title":"Service assembly"}
```

| Argument | Values |
| --- | --- |
| `exploded_view`, `explode_factor` | view name and factor 0..1 (default 1) |
| `motion`, `motion_time_s` | baked motion and time 0..600 s; the nearest earlier baked frame and any `explode_view` channel of that motion apply |
| `view`, `camera`, `projection` | `iso`, `front`, `right`, `top` (Z up, as in `snapshot`) or `custom` with `camera{direction,up,target_mm?,distance_mm?,fov_deg?}`; `perspective` or `orthographic`; the model is fitted unless `distance_mm` is given |
| `lighting` | `studio`, `soft`, `dramatic`, `technical` (outlines and crease lines) |
| `background` | `light`, `dark`, `white` |
| `resolution` | `640x400`, `1024x640` (default), `1280x720`, `1280x800`, `1600x900`, `1920x1080` |
| `samples` | 1, 4 (default), 9 or 16 jittered passes per pixel |
| `balloons`, `bom_table`, `title` | assembly drawing overlay and an optional caption |

Balloons carry `bom_inspect` item numbers. Each item's balloon attaches to the
visible pixel of its most visible occurrence nearest that occurrence's visible
centroid; hidden items are listed in `balloons_not_visible`. Balloons sit on the
model silhouette box grown by three radii and are pushed apart; the table takes
the corner that covers least of the model. Every image carries the footer
"ARK Visualize / display tessellation, not geometric authority". The evidence
reports revision, view state, lighting, background, samples per pixel, the
exploded and motion state, balloons with occurrence identity and the BOM rows.

A stale bake rejects with `VISUALIZE_MOTION_STALE` (bake again); asking for a
view the motion already drives rejects with `VISUALIZE_EXPLODE_CONFLICT`;
unsupported arguments reject with `VISUALIZE_INVALID`; more than 2,000,000 display
triangles reject with `VISUALIZE_CAPACITY`.

## Studio

**Model > Explode** (the Assembly ribbon group; More in the compact ribbon) or
the tool gallery's Motion group opens the **Exploded Views** panel docked right
of the viewport. It shares the dock with Baked Motions; one is open at a time.
An agent's `explode.set` opens it on that view, collapsed.

- **Explode tab.** Views list; New view, Rename and Delete; the explode factor
  slider with tick marks at slice boundaries; Collapse, Animate (plays collapsed
  to exploded) and Explode. The step list shows instances, direction, the
  evaluated distance and `with previous`; Edit, Delete, Up and Down. Clicking
  components in the viewport collects them for the next step; Add step opens
  the native editor with instances, direction (axes, Toward camera, Screen
  right, Screen up or typed `x, y, z`), distance expression, run with previous
  and label. Every edit is one revision-checked, undoable `explode.set`.
  Moving the slider or animating never changes the revision.
- **Exploded display labelling.** While exploded, the viewport shows an
  "EXPLODED DISPLAY" banner. Measurement refuses to start ("Return to the
  formed, stationary model"), and starting a component move collapses the view
  first. Picking still selects the correct occurrence identity. Sheet flat,
  stock, drawing and mechanical motion modes collapse the explosion.
- **Visualize tab.** Engine: GPU viewport (the existing progressive path tracer
  when available, raster otherwise, with its reason) or Software (the
  `visualize_render` renderer at exact size). Look (Studio, Product, Technical),
  background (Dark, Light, White), resolution (Viewport, 1280x720, 1920x1080,
  2560x1440; Software caps at 1920x1080), target samples with the live sample
  count, balloons and BOM table previewed in the viewport, and Export PNG.
  The presets hide grid and view cube and are restored when Visualize closes.
  The GPU export reads the 3D pass before sketches, selection and UI, crops it to
  the preset aspect and reports when it had to upscale viewport pixels. The
  current pose is used: assembled, exploded at the slider factor, or the baked
  motion frame (Stop returns to the assembled pose).

## Persistence

Views persist as the optional top-level `exploded_views` array in JSON and
`.ark` containers. Documents without it load unchanged and removing every view
removes the field. The document format version is not changed: earlier ARK
readers ignore the field and would drop it on save. Structural checks run on
load; instance existence is checked by authoring.

## Agent protocol

Agents author exploded views and produce `visualize_render` images only on an
explicit user request for an exploded view, assembly image or assembly drawing,
the same rule as 2D drawings (B.05). `explode_info` discovery stays available.
Load `agent_onboard{topic:"exploded_views"}`; the motion topic points to the
`explode_view` binding. Both tools are read-only and listed under the
`exploded_views` specialist topic.

## Limits and scope

- Translation steps only; no per-step rotation, trails or exploded line
  sketches. Directions are world vectors.
- Exploded positions and images are display evidence. Clearance, mass
  properties, drawings and exports use the assembled design.
- Studio GPU balloon anchors come from viewport ray picks over display bounds
  (bounded to 4000 picks per layout); the software renderer uses visible pixels.
- The path tracer refuses textures, stamps, transparency and emission and falls
  back to raster with its reason, as before. The software renderer adds
  emission without bloom and keeps textures and stamps as in `snapshot`;
  transparency is sorted per triangle.
- The 2D drawing workspace does not show exploded states in 048B.

## Verification record

Local diagnosis toolchain only: llvm-mingw (MinGW) with the unpatched OCCT SDK,
not a release build. Synthetic fixtures only; no user design is a fixture.

- `tests/exploded_view_048b_tests.cpp` (CTest `exploded-view-048b`) passed
  **234 checks**: authoring with names, axes and expressions, factor math over
  sequential and shared slices, group and accumulated offsets, every rejection
  above with its `error_code`, atomic batches, 16-view capacity, JSON and
  binary `.ark` round trips, a document without views, persisted duplicate-name
  and zero-direction rejection, undo/redo, missing instances after
  `assembly.remove`, unchanged revision and document across factor queries and
  renders, `explode_info` list/detail/factor/propose (a proposal commits
  unchanged), the motion binding and its bake (factor 0, 0.5 and 1 at frames
  10, 15 and 20 of a 10 fps bake beside a prismatic joint), every motion
  rejection above, explode-only bakes, rendering at a baked time, stale and
  conflict rejections, PNG signature and IHDR size for 1024x640, 1280x720 and
  640x400 renders, BOM numbering with quantities and part numbers, balloon
  anchors, overlay layout and the onboarding topics.
- The rendered PNGs were inspected: technical white iso with balloons and BOM,
  dark front, orthographic custom and a baked-motion frame.
- `studio-exploded-views-ui` (`--self-test-exploded-views-ui`) passed at
  1280x800, 1440x900 and 1366x800: agent `explode.set` opens the panel, slider
  slices, unchanged revision and document, measurement refused while exploded,
  collapse, Animate monotonic to 1, viewport pick plus Add step through the native
  editor, undo, a baked `explode_view` channel combined with a prismatic joint,
  Visualize presets and restoration, and PNG export at viewport size, 1280x720
  (GPU, path traced) and 1920x1080 (software). Captures and exported PNGs were
  inspected for overlaps and clipping.
- `ark-mcp` over stdio listed 104 tools with both new tools read-only,
  committed an `explode.set`, returned offsets from `explode_info` and an image
  block from `visualize_render`.
- Local full CTest on this toolchain: 147 of 150 passed. `distribution-fixtures`
  (`cmake` not on PATH), `native-solid` and `native-surface` (unpatched OCCT)
  are the known local failures of this diagnosis toolchain. The existing
  tool-count assertions moved from 102 to 104 tools in the feature branch, and
  to 106 with the parametric HUD tools of the same iteration. Studio selftests
  `baked-motions-ui` (also 1366x800), `measurement-ui`, `viewport-dock-ui`,
  `action-icons-ui`, `render-ui`, `history-ui`, `realtime-ui` and
  `interaction-ui` (1440x900, source directory) passed.
- Not run: the MSVC release build against the patched OCCT SDK, hosted CI and
  integration with other iteration changes.
