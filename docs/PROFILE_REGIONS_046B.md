# 046B — profile regions: holes, islands and hollow sweep sections

**B.02/B.03 — Verificado no escopo local** described below; both milestones
remain **Parcial** outside it. A sketch with several non-touching closed loops
is now one profile: a plate outline with holes extrudes in one feature, and an
outer wire with inner loops sweeps as a hollow section. Before 046B these
sketches were rejected:

| Consumer | Earlier rejection (reproduced on 045A `e8ca022`) | Location |
|---|---|---|
| `native_solid` extrude | `Native extrude: Profile wire does not close` | `src/occt/solid_modeling.cpp` `profileFace` (one `BRepBuilderAPI_MakeWire` for all entities) |
| legacy mesh `extrude` (Studio *Extrude sketch*) | `Nested sketch loops require separate cut features`; disjoint loops: `Extrude/revolve requires one outer loop` | `src/sketch.cpp` `sketchProfiles`, `src/evaluate.cpp` `makeProfile` |
| `extrude_surface`, `revolve_surface`, `sweep` section | `profile entities are not connected in supplied order` | `src/occt/surfacing.cpp` `profileOf` |

## Contract

**When it applies.** The host counts endpoint-connected groups of the sketch's
non-construction curves. One group without `native.regions` keeps the earlier
single-loop request byte for byte. Two or more groups, or an authored
`native.regions`, add a `regions` block to the profile; the OCCT worker then
resolves everything from exact edges.

**Loops.** Edge ends join only at a shared vertex or within the sum of their
own vertex tolerances. Every joint must join exactly two ends. Each closed
chain is one loop, built as one wire and one planar face that must pass
`BRepCheck_Analyzer` and have positive area.

**Containment (even-odd).** Loops may not intersect or touch (exact
`BRepExtrema` distance above the sum of vertex tolerances). The depth of a loop
is the number of loops containing it (exact face classification). Depth 0 is an
outer boundary, 1 a hole of its immediate container, 2 an island, and so on.
A region is one even-depth loop minus its direct odd-depth children. Its face
is checked again: valid B-rep, and area equal to outer minus holes within
`max(1e-9 mm², 1e-9 × outer area)`.

**Order and references.** Loops are ordered by depth, then by their first
authored profile edge (sketch entity order); regions follow their outer loops.
Faces and edges keep the existing producer names derived from sketch entity
names, so hole walls are `side.<entity>` (`side.h1`, `side.slot.0`,
`side.window.2`) with `edge.start.<entity>`/`edge.end.<entity>`, plus
`cap.start`/`cap.end`. Names do not depend on loop order and stay identical
under dimension edits that keep the topology. The receipt
`profile_regions` (in `report.extrusion` for `native_solid` extrude, in
`report` for the surfacing consumers) lists every loop (`index`, `depth`,
`role`, `parent_loop`, `region`, `entities`, `edges`, `area_mm2`) and region
(`outer_loop`, `hole_loops`, `area_mm2`, `selected`, `selected_by`).

**One solid per feature.** Every consumer below already publishes one solid.
Several regions (disjoint outer loops, or an island inside a hole) therefore
reject with the outer-loop entities named, unless `native.regions` selects
exactly one. ARK does not union, drop or split them silently.

**Selection** (`native` of the consuming feature):

```json
{"op":"feature.add","type":"native_solid","name":"Boss",
 "native":{"op":"extrude","regions":[{"point":["W/2","H/2"]}]},
 "p":{"sketch":"Nest","depth":"8"}}
```

- `{"entity":"name"}` names a curve of the region's **outer** loop. It is
  invariant under dimension edits that keep the topology.
- `{"point":[x,y]}` is a seed in sketch coordinates; numbers or expressions,
  so a seed authored from the same parameters follows them.
- 1..64 seeds, each exactly one selector. Regions are taken in region order,
  not seed order.
- Rejected, naming the seed: a point within `max(1e-6 mm, vertex tolerance)` of
  a loop, inside a hole, or outside every loop; an entity that bounds a hole,
  an open chain, several loops, or is construction/point/unknown/ambiguous; two
  seeds selecting one region; an empty list; `regions` on any other operation
  (loft, box, union, …), on an exact `source_face` or inline profile, or on a
  surface (`solid:0`) result.

**Rejections without a selection:** crossing or touching loops, loops sharing
a vertex or branching, self-intersecting/zero-area/collinear loops, and open
chains mixed with closed loops. With a selection, an open chain that does not
touch any loop is ignored and listed in `open_chains_ignored`; one that touches
or crosses a loop rejects. No tolerance is enlarged.

## Consumers

| Feature | Behaviour |
|---|---|
| `native_solid` `extrude` with `p.sketch` | Prism of the region face for every end condition: `distance`, `symmetric`, `direction` ±1 and `up_to_face` (the limit must cover the whole cap, holes included). |
| legacy `extrude` (mesh, Studio *Extrude sketch*) | One region with holes reuses the validated full-depth prismatic path (area × height volume, closed mesh, Mapbox Earcut caps). No `native` selection is stored for this mesh type; several regions reject. Its exact promotion (when a native operation consumes it) builds the region face. |
| `extrude_surface` / `revolve_surface` with `p.solid:1` | Prism / revolution of the region face. A closed hole revolves to an enclosed void shell inside one solid. |
| `sweep` with `native.section` sketch and `p.solid:1` | Hollow section, see below. |
| legacy `revolve` (mesh) | Keeps one loop; several loops reject and point to `revolve_surface`. |

**Hollow sweep.** Each loop of the one selected region is swept by its own
`BRepOffsetAPI_MakePipeShell` with the same path, orientation mode
(`corrected_frenet`, `frenet`, `fixed_binormal`, `support`, `auxiliary`) and one
shared placement, then the hole solids are cut from the outer solid by an exact
Boolean. OCCT's per-section correction would rotate every loop about its own
centre, so it is not used per loop: with `correction:1` (default) a section
whose sketch normal is not parallel to the initial path tangent is rotated once,
rigidly, about the region's area centroid (the existing straight-section rule);
`correction:0` sweeps it as authored. `contact:1`, `laws` and varying
`sections` reject. The result must be one valid solid whose volume equals the
outer sweep minus the hole sweeps within `max(1e-5 mm³, 1e-7 × outer)`;
otherwise it rejects (a hole leaving the outer sweep, or loops overlapping
along the path). `report.hollow_section` records the loop count, each swept
volume, the conservation error and limit, and the correction applied.

## Persistence

`native.regions` is the only new persisted field. It selects document source
**48**; a file below 48 carrying it rejects on load (also through iteration
history), and 49 and later reject as future formats. Multi-loop sketches
consumed without `native.regions` add no field and keep their earlier required
version (the test document stays at 12). The `.ark` container schema and the
iteration journal format are unchanged.

## Agent and Studio surface

`agent_onboard{topic:"sketch"|"surfacing"|"solids"}` carries one compact
046B rule; the sketch rules no longer say that inner loops require
`sketch_cut`. `sketch_inspect` reports `profile_ready:true`, the loop count and
`profile_regions` (sampled preview of outer loops, depths and holes) for
multi-loop sketches; `doc_info{detail:"design"}` `profile_ready` follows the same
analysis. Studio needs no new control: *Exact sketch extrusion*, *Drag extrude*,
*Extrude sketch* and *Sweep* accept these sketches through the same requests,
and the sketch overlay already fills regions even-odd. Studio has no region
picker; seeds are authored through `doc_apply` or the command editor.

## Local evidence (046B branch, Windows, OCCT 8.0.1)

See the integration record for full-suite counts. `ark-profile-regions-046b-tests`
(CTest `profile-regions-046b`) checks, through revision-checked `doc_apply`:

- legacy identity: B-rep and reference digests of single-loop `native_solid`
  extrude (wire with arc; circle symmetric/reversed), `extrude_surface` (slot
  recipe), `revolve_surface`, `sweep`, the legacy mesh extrude bytes and its
  exact promotion equal those recorded from the untouched 045A build. The
  digests were recorded with Windows MSVC and OCCT 8.0.1 and are compared only
  there: the Linux build serializes the symmetric circle extrude to different
  bytes, so other toolchains check only that legacy cases stay on the
  single-loop path;
- plate 100×60×10 with four r5 holes and every extrude end condition;
  dimension edits keep `side.h1..h4` names and order; legacy mesh extrude and
  its exact promotion;
- nested island (depths 0/1/2) rejected without a selection, frame and island
  regions selected by entity and point;
- two disjoint outer loops rejected on native, surface and mesh extrude;
  selected individually;
- every seed rejection listed above and an expression seed that follows a
  width edit;
- hollow sweeps: straight round tube, line + quarter-arc tube (Pappus), square
  tube with a round bore, `correction:0`;
- `revolve_surface` 360° and 180° of a rectangle with a closed circular hole;
- crossing, tangent, shared-vertex, open-chain, crossing open chain,
  self-intersecting and collinear loops on the native and mesh paths;
- clockwise outer/hole wires, slot recipe and circle holes;
- a 10×10 hole grid;
- format 48 round-trip with and without history, cold rebuild parity, 47
  rejection, 49 rejection, format restored after removing the selection;
- `sketch_inspect` regions and onboarding text.

Measured in that run (single local runs on one machine, B-rep volume
integration, not a performance qualification):

| Case | Analytic | Exact result | Time (`doc_apply`) |
|---|---|---|---|
| plate 100×60×10, 4 × r5 holes | 56858.407 mm³ | error −2.2e−11 mm³ (mesh path 56894.2 mm³, sampled at 0.05 mm) | 4.7 ms |
| island r15 × 8 in a window | 5654.867 mm³ | equal within 1e−9 relative | |
| round tube r10/r8, 100 mm straight | 11309.734 mm³ | error 1.8e−12 mm³ | 4.4 ms |
| same tube, 50 mm line + R50 quarter arc (Pappus) | 14537.511 mm³ | error 0 | 15.0 ms |
| ring section 20×30 with r5 void, 360° revolve (Pappus) | 98292.929 mm³ | error 1.5e−11 mm³ | |
| 210×210×3 plate with 100 × r4 holes | 117220.355 mm³ | error 5.8e−10 mm³, 106 faces | 35 ms |

The Studio check `studio-parametric-authoring-ui` arms *Drag extrude* on a
plate sketch with five holes and drags the exact extrusion. The sketch overlay
fill area must equal the region area (sampled, within 0.5 %), and the preview
and saved body volumes must equal area × dragged depth within 1e−9. The captures
`profile-regions-sketch`, `profile-regions-extrude-preview` and
`profile-regions-extrude-saved` were inspected: the overlay leaves the five
holes unfilled, the preview tessellation shows the five hole walls, and the
saved body has five through holes.

## Not established

- Region selection in Studio by clicking a region; holes in the legacy mesh
  `revolve`; regions in loft, surface fill, split-line or hole-cutter profiles;
  varying-section and law sweeps with holes.
- Several solids from one feature (islands or disjoint outer loops) — they
  require one feature per region.
- Hollow sweeps whose section is placed away from the path start with a
  non-normal plane: the shared correction uses the initial path tangent, not
  OCCT's per-section placement parameter.
- Exhaustive containment for loops closer than OCCT vertex tolerances, and
  performance beyond the measured local cases. No manufacturing, tolerance or
  cost claim is made.
