# Additive and three-axis CNC manufacturing

060C adds a native C++ manufacturing workspace to the same document, revision,
preview/apply, graph and agent contracts used by CAD. **A.02/A.04/C.03/C.04/D.02/E.01
remain partial overall.** This implementation is a bounded simulation and
planning slice, not a complete industrial CAM/controller qualification.

## Source and graph contract

`Document.manufacturingPlans` is an optional array of at most 32 plans. A plan is
`{name, process: "cnc" | "fdm", nodes: [...]}`. Each node has a stable ASCII
`id`, `kind`, `data`, optional `name` and `depends_on` IDs. Typed references create
graph edges as well. Missing references, wrong kinds and cycles reject atomically.
Plans retain at most 256 nodes and fit the existing document capacity. Documents
with this authored field use semantic format **53**; older documents retain their
existing format. Simulation frames and stock samples are derived, never CAD.

| Node kind | Data and coordinate contract |
| --- | --- |
| `part` | `feature`, `source_sha256`: exact B-rep binding to one evaluated valid solid. |
| `machine` | `model: cartesian_xyz`, `min_mm`, `max_mm`, `home_mm`, `tool_change_mm`, `rapid_mm_min`, `tool_change_s`. XYZ travel is the spindle/gauge position. |
| `machine_body` | `machine`, `min_mm`, `size_mm`, `axis: fixed | x | xy | xyz`; optional exact CAD `feature` and `source_sha256`. Bounds are explicitly declared envelopes. |
| `collision_pair` | `a`, `b`: machine-body, fixture or stock node IDs to screen for relative swept envelope overlap. |
| `stock` | `min_mm`, `size_mm`, `resolution_mm`, optional `density_kg_m3`. Axis-aligned box in the machine frame. |
| `fixture` | `min_mm`, `size_mm`, `initially_clamped`. A clamping event changes the declared holding state; it does not synthesize jaw geometry. |
| `tool` | `number`, `kind: flat_end_mill`, `diameter_mm`, `cutting_length_mm`, `gauge_length_mm`, `holder_diameter_mm`, `holder_length_mm`. |
| `setup` | `part`, `machine`, `stock`, `fixtures`, `origin_mm`, `work_offset: 54..59`, `clearance_z_mm`, `setup_s`. Work offsets are translations; rotated/indexed setups are not implemented. |
| `operation` | `setup`, optional `tool`, `post`, `rates`, plus a supported operation below. |
| `post` | `dialect: fanuc-3axis`, `decimals: 3..6`, `program_number: 1..9999`, `line_numbers`. Generates a documented subset, not arbitrary proprietary post scripts. |
| `rates` | `currency`, `machine_hour`, `operator_hour`, `material_kg`, `power_kw`, `energy_kwh`, `markup_percent`. Explicit declarations; zero template rates mean pricing unavailable. |
| `slicer` | `engine: bambu-studio-cli`, `executable`, `machine_profile`, `process_profile`, `filament_profile`. Full local JSON profiles are required. |

Operations execute in stable topological order, with authored node order breaking
ties. Use explicit `depends_on` links between sequential machining operations.
Machine envelopes, process dimensions, stock and rates in example plans are
editable examples, not inferred machine capability or a production recipe.

- `face` and `pocket`: rectangular bottom `min_mm`, machining `size_mm`,
  `stepover_mm`, `stepdown_mm`, `feed_mm_min`, `plunge_mm_min`, `rpm`. A bounded
  raster uses flat end mills, explicit plunges and clearance retracts.
- `drill`: `points_mm` are final tip XYZ targets, with explicit plunge/feed/RPM.
  This is a flat-end-mill plunge path, not a twist-drill point or canned cycle.
- `clamp` / `unclamp`: `fixture`, `duration_s`; requires stopped spindle above
  setup clearance. CNC export represents this as a manual `M0` instruction.
- `gcode`: inline `gcode`, or a pinned `source_path`, `source_sha256` and optional
  `source_member` for a sliced 3MF. Import embeds programs up to 64 KiB; larger
  programs retain an external hash-bound artifact reference. Keep those files
  with the project. Modified or missing program bytes reject rather than rebase.

## Agents and the shared graph

Read `agent_onboard{topic:"manufacturing"}` and
`manufacturing_plan{action:"capabilities"}`. The latter needs no revision. Other
actions require the observed `expected_revision`:

1. `manufacturing_plan{action:"example",feature,process,...}` or
   `action:"prepare",plan` returns **unexecuted** `preview_arguments`.
2. Configure the declared inputs; call `doc_preview` and inspect its checks.
3. Publish the retained candidate with `doc_apply`.
4. `manufacturing_plan{action:"inspect",name,...}` exposes node fingerprints,
   source freshness and edges; embedded programs are compacted to hashes/counts.
5. `manufacturing_simulate{name,...,offset?,limit?}` returns bounded move pages,
   diagnostics, events, cost basis, graph/source/result hashes and revision.
   A source- and revision-bound `result_id` shares immutable derived evidence
   across Studio and agents. Pagination reuses at most four cached studies within
   128 MiB; exact sources and external program bytes are checked before reuse.
6. `manufacturing_export{name,path,format:"nc",...}` checks the supported
   simulation before generating a **new** local file. Imported G-code is already
   posted and is simulation-only; retain its original bytes.

Incremental graph edits use `manufacturing.node.set{name,node}` and
`manufacturing.node.remove{name,id}` in `doc_apply`. Whole-plan operations are
`manufacturing.set{plan}` and `manufacturing.remove{name}`. They participate in
preview, rollback, undo/redo and native persistence. General/orchestrator agents
own plan publication; reviewers remain read-only. An exact source change requires
an explicit `manufacturing_plan{action:"refresh_source",name,...}` proposal and
reviewed preview/apply. No previous simulation remains current automatically.

Manufacturing nodes and CAD source edges appear in `context_graph` and the native
project graph. The existing context fingerprint/known-source mechanism covers
these nodes. `manufacturing_export{format:"obsidian",name,path,...}` writes a
new linked Markdown snapshot plus a SHA-256 manifest. It is derived navigation
context; existing vault files are not modified and the vault cannot edit CAD.

## Native Studio

The **Manufacturing** ribbon opens a modeless workspace with a graph-node list,
stock/setup/workholding/tool/machine/operation/rate/post editors, G-code import,
Bambu slicing, simulation, playback, step controls and exports. Double-click a
node to edit its supported fields. The project graph button opens the existing
shared graph view. The simulation runs against a private document snapshot;
edits invalidate its result instead of publishing a stale study.

Viewport cyan paths are CNC feed moves, gray paths rapid moves, pink paths FDM
deposition, orange points sampled voxel stock, and green wires the current tool.
Playback is a derived machine/feature-local view, independent of CAD assembly
placement. The original CAD is not cut or moved by playback. Evidence identifies
the declared resolution and sample method. Stop/Cancel cancels private work.

## Simulation scope

The interpreter handles the supported modal subset: G0/G1, XY G2/G3 with IJ or
R, G17, G20/G21, G90/G91, incremental arc centres G91.1, G94, G40, G80, G53,
declared G54–G59, matching G43 H tool length, G49, G4 P milliseconds, spindle,
coolant, program stops and T/M6 events. CNC paths with active tools require
explicit matching tool-length compensation. Units, distance mode, XY plane and
feed mode must be declared before CNC motion. Unsupported codes, macros, canned
cycles, compensation modes or ambiguous syntax reject. This is **not** an
emulator of every Fanuc control variant. Repeated or unknown modal state is not
silently guessed.

Stock removal uses continuous swept flat-end-cylinder membership at voxel
centres. Each box has at most one million cells, with two million combined;
the receipt gives actual per-axis cell size. Fixture/holder and configured
machine-body pairs use conservative axis-aligned swept envelopes. Rapid checks
use remaining occupied stock cells. XY arcs are chordal to 0.02 mm. The
simulation retains at most 100,000 moves/events and bounds cell work explicitly.
Unsupported size or work rejects; resolution is never silently coarsened.

Not implemented: exact target-gouge/remaining-tolerance verification, 4/5-axis
or turning, general tool shapes, full toolchanger-arm motion, cutting forces,
fixture deformation, spindle/servo dynamics, acceleration/lookahead matching,
thermal behavior, chip evacuation or machine-specific production qualification.
`target_gouge_checked`, `full_machine_collision_checked` and
`manufacturing_qualified` remain false. Configured body-pair checks report their
declared subset, not full-machine coverage.

## Bambu and cost provenance

The C++ bridge invokes the installed Bambu Studio CLI with explicit full machine,
process and filament profiles and a source-derived STL. It uses no shell, writes
to a new output directory, snapshots the supplied profiles before invocation,
limits/cancels the process tree, and records source,
mesh, executable, profile and output hashes. It does not upload to a cloud,
connect to a printer or copy Bambu code into ARK. The external slicer retains its
own license and installation. Native ZIP/deflate import validates member size
and CRC; multiple G-code plates require explicit member selection.

FDM analysis handles absolute/relative extrusion, G92 E, retraction recovery,
linear/arc trajectories and layer-height context. Unknown vendor or malformed
blocks are retained as diagnostics and prevent a complete motion interpretation
claim. The reported path may therefore be partial. Bambu's declared time/mass
comments, when present for every program, can separately supply a price estimate
with `time_authority: slicer-declared`; they are not an independently measured
print. Otherwise cost uses the ARK kinematic lower bound only when interpretation
has no blockers. Heating and acceleration are not fabricated. No price is shown
when required rates/evidence are unavailable.

Primary interface references:

- [Bambu Studio CLI](https://github.com/bambulab/BambuStudio/wiki/Command-Line-Usage)
- [Bambu Studio license](https://github.com/bambulab/BambuStudio/blob/master/LICENSE)
- [LinuxCNC G-code semantics reference](https://linuxcnc.org/docs/stable/html/gcode/g-code.html)

LinuxCNC is a reference for the common language vocabulary, not proof of Fanuc
machine compatibility. The post and the actual controller/setup need independent
verification before physical machining.
