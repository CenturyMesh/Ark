# ARK agent protocol

The current API adds `suspension_inspect` for read-only double-wishbone poses.
Call `agent_onboard{topic:"suspension"}` before authoring its version 18 program.
It returns joint residuals, wheel attitude and ideal spring force; the feature
is a kinematic display model, not an exact B-rep or dynamics solver.
See [suspension](SUSPENSION.md).

The latest sheet/showcase extension has **44 tools**. `sheet_inspect` and
`export_flat_dxf` inspect/export authored flat sheets; `stock_preview` returns
nominal straight tube/frame stock; `usage_info` and `usage_record` manage
persisted drawing token receipts. `sheet.opening.pattern` adds bounded
expression-driven copies, and sheet openings support circle/slot/window/polygon.
Usage metadata and slot/window documents use version 17. See
[KITE showcase and counters](KITE_SHOWCASE.md).

The earlier engineering extension brought the API to 38 tools; the latest additions above bring it to 44. New feature types
`construction_frame`, `pcb` and `cable_route` use document version 16.
Call `agent_onboard` with `frames`, `electronics` or `routing` before designing
them. `frame_inspect` and `electronics_inspect` return compact evaluated
evidence; `pcb_export` writes fabrication previews. `export_iges` provides
exact IGES exchange and `export_mesh` accepts `fbx` as well as `obj`/`stl`.
See [engineering modules](ENGINEERING_MODULES.md), [exchange](EXCHANGE.md),
and [CPU/GPU execution](COMPUTE_ARCHITECTURE.md).

The current material/rendering extension adds `agent_onboard{topic:"materials"}`,
persisted materials and appearance, and exact-solid mass/weight/centroid reports.
Use `material.set{name,density?,appearance?}` and `material.remove{name}` in
`doc_apply`; assign `material` or `appearance` through `feature.add/edit`.
Appearance-only batches preserve the geometry cache and report
`geometry_recomputed:false`. `native_solid` also supports `split_plane` and
mirror `p.copy_only=1`. These additions use document version 15 without adding
tools. See [materials and rendering](RENDER_MATERIALS.md) and
[exact split examples](SURFACING.md#exact-plane-splits-and-copy-only-mirrors).

The C++ MCP host reads one JSON-RPC message per line on stdin and writes protocol responses only to stdout. Tools return both `structuredContent` and its JSON text representation, with `isError` set on failures, following the [MCP tool result specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools). Transport behavior follows [MCP stdio](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

Initialize with protocol `2025-11-25`, `2025-06-18` or `2024-11-05`. Each process owns one document. The API is synchronous; a long operation occupies that session. Applications must not call a single `AgentSession` concurrently.

## Recommended modeling loop

1. Call `agent_onboard` for capabilities, current document and revision; request the relevant topic. Use `doc_get` only when full model details are needed.
2. Form a batch of `param.set`, `feature.add`, `feature.edit`, `feature.remove` or `sketch.set` commands.
3. Call `doc_apply` with `expected_revision`. Any evaluation failure leaves the existing document and revision intact.
4. Use `mesh_inspect` and `mesh_raycast` for compact evidence. Request `mesh_get` only when full positions/indices are necessary.
5. Generate a `snapshot` for visual feedback and `doc_save` to preserve intent. Export a mesh only when needed.

```json
{
  "tool": "doc_apply",
  "arguments": {
    "expected_revision": 0,
    "commands": [
      {"op":"param.set", "name":"width", "expr":"60"},
      {"op":"feature.add", "type":"box", "name":"Base", "p":{"dx":"width","dy":"width/2","dz":"8"}}
    ]
  }
}
```

The envelope above is accepted by Studio's command editor and `.arkops` replay. To use MCP, wrap the tool name and arguments in `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"doc_apply","arguments":{...}}}`.

Use `created_ids` returned by a batch as inputs to later features. IDs are monotonically allocated within a document and persisted, including its next-ID counter. Inside a batch, `feature`, feature `inputs`, and mate endpoints `a`/`b` accept a unique name or an id. The saved document stores the numeric id. A missing or ambiguous name fails the batch and rolls it back. Features must follow their dependencies in history. Removal of a dependency without also removing/replacing its dependants fails atomically.

## Tools

| Tool | Purpose |
| --- | --- |
| `doc_info`, `doc_get` | Compact summary, or `detail:design` for names, values, volumes and bounds without expressions or meshes / full parametric document |
| `doc_apply` | Atomic modeling batch |
| `doc_undo`, `doc_redo` | In-memory transaction history; revision increases |
| `eval_expr` | Finite arithmetic and trig evaluation |
| `mesh_inspect` | Indexed topology, measurements and explicit validation limits |
| `mesh_raycast` | Nearest triangle hit, normalized direction, distance in mm |
| `mesh_get` | Positions and triangle indices, explicitly requested |
| `doc_save`, `doc_load` | Native document persistence |
| `export_mesh` | STL/OBJ of terminal outputs, a selected feature ID, or a unique instance name plus `copy` |
| `snapshot` | Software-rendered PNG of terminal outputs, a selected ID, or a unique instance name plus `copy`. `view` is `iso`, `front`, `right` or `top`. The image is a display mesh, not STEP authority |

The process has local filesystem permissions of the host. Save/export tools write the supplied paths. File saves replace existing documents; there is no cross-process file locking. MCP sessions are local, not a remote HTTP service.

## Numerical contract

Document lengths use millimetres. `sin`, `cos`, `tan`, `asin`, `acos`, `atan2` operate in radians; `deg(x)` converts degrees to radians. Transforms' `rx/ry/rz` and revolve's `angle` use degrees. Expressions support `+ - * / ^`, parentheses, scientific notation, `sqrt`, `abs`, `min`, `max`, `pow`, `floor`, `ceil`, `pi` and `e`. Use explicit parentheses around negative bases when exponentiating.

Surface coordinates use `u` and `v`; defaults are domains `[0,2*pi]`, with `nu=96`, `nv=48` subdivisions. Resolution is limited to 3..256 per axis. `wrap_u=1` and `wrap_v=1` request shared seam indices; sampled endpoints must agree within 1e-6 mm. This is a sampled seam check, not a symbolic continuity proof. Mesh buffers use f32, while expressions and analytic geometry use f64.

Reports use indexed connectivity without silently welding/healing. `closed_oriented_edges=true` only means the implemented edge, degeneracy and finite-data checks passed. `solid_certified=false` and `self_intersections_checked=false` remain explicit. Signed volume is provided only for meshes with closed oriented edges and is not a certified physical volume. Triangle IDs belong to one tessellation; do not retain them across edits.

## Capacity and performance

Documents are limited to 4 MiB on load, 1024 features, 256 parameters and 64 sketches. A sketch accepts 128 entities and 256 constraints. JSON nesting is limited to 64; mathematical expressions to 2048 characters. These are request limits, not a bound on all possible boolean or solver runtime. There is no cancellation or worker isolation yet.

Mesh buffers, reports and BVHs are cached per session revision and selected feature ID. One geometry recompute handles the whole batch. History stores up to 32 serialized document states; evaluated meshes are not duplicated in undo history. The old hand-written SIMD routines are retained as source archaeology but are disabled until ABI/numerical parity and profiling justify them.

## 0.3 additions

`assembly_info`, `tube_inspect`, `compute_info` and `compute_benchmark` bring the tool count to 17. `doc_apply` accepts `assembly.add`, `assembly.edit`, `assembly.remove`, and tube features with a top-level `bends` array. Mesh tools accept `node` for world-space instances or `id` for local part definitions, never both. Full contracts and examples are in [MECHANICAL.md](MECHANICAL.md). Version-2 `.ark` documents preserve the assembly hierarchy and bend programs. The compute backend is optional, with explicit capability reporting and CPU fallback.

## 0.4 preview operation

`notch_plan` was the 18th tool in 0.4. It returns a standalone `ark-cut-plan` for an ideal zero-radius retained-wall notch, with cutter geometry and mathematical checks. It does not edit the active document or claim source reconstruction/physical validation. The current C++ `step_inspect{path,faces?}` tool provides a read-only OCCT inventory of STEP sources; the former Python audit script has been superseded.

## 0.5 integrated workspace

`notched_tube` is now an editable feature with stock expressions and a replaceable `notches` array. `notch_inspect{id,progress?}` reports the plan and optional display transforms. `export_step{id,path}` writes the straight part using the configured OCCT backend and reports its validation. These additions bring the tool count to 20. Animation is read-only and cannot change the exported pose. See [BEND_WORKSPACE.md](BEND_WORKSPACE.md).

## 0.6 source-preserving import

Use `import_step{path,replace?}` for the bounded planar finite-hinge source domain. `source_inspect{id,progress?}` provides the measured projection/holes without the large payload of `doc_get`. `export_step` now handles `source_tube` as well as designed `notched_tube`. The source geometry is read-only and the imported package is version 4. These additions bring the tool count to 22. See [SOURCE_STEP.md](SOURCE_STEP.md) for validation and failure boundaries.


## 0.7 parametric sketches, onboarding and live Studio

`agent_onboard{topic}` is the first tool to call. Topics `overview`, `sketch`,
`tube` and `assembly` provide compact current-state context, supported operations,
validation boundaries and relevant examples. The sketch example includes the
current `expected_revision` and can run on a fresh document.
`doc_new{expected_revision}` starts an empty document with undo and a monotonic
revision; it never resets revision to zero or overwrites saved files.

`plane_inspect{name}` and `sketch_inspect{name,samples?}` bring discovery to
**26 tools**. `doc_apply` adds `plane.set`, `plane.remove`, `sketch.remove`,
`sketch.entity.set`, and `sketch_cut` features. Construction planes, driven
coordinate/dimension expressions and interpolating splines are specified in
[PARAMETRIC_SKETCH.md](PARAMETRIC_SKETCH.md). Inspections do not mutate revision.

A standalone `ark-mcp` still owns its own document. `ark-mcp --connect <endpoint>`
uses the live Studio bridge; all tool calls then execute on Studio's owning
thread and share its document, revision and undo history. Disconnect never
falls back to an unrelated empty session. [Codex configuration](CODEX_CONNECT.md).

The in-app assistant uses a configured Responses-compatible provider and these
same tools. [Connector contract and testing](ASSISTANT.md). Onboarding and
geometry operations also work without an API key or an LLM.

## 0.8 native OCCT modeling and sheet sources

The 0.8 API had **30 tools**. `agent_onboard` added `surfacing` and `sheet`
topics to its earlier overview/sketch/tube/assembly topics. `kernel_info{}` reports the native OCCT
version and capability status. `step_inspect{path,faces?}` inventories an
original STEP file without editing the document. `native_inspect{id}` returns
compact native B-rep validation and opening evidence without transmitting
large mesh or B-rep buffers. `import_sheet_step{path,k_factor,body_index?,
root_panel?,replace?}` imports a supported source sheet with an explicit
K factor; a multiple-body file requires a zero-based body index, and equal
flanges can require root panel 0 or 1.

`doc_apply` supports authored `loft`, `surface_fill`, `bspline_surface`,
`thicken`, and `sheet_metal` features. Use `native` for loft sketch names,
NURBS poles/weights, advanced fill profiles, and sheet openings; use `p` for
editable expressions. Source sheets are read-only. `export_step{id,path,pose?}`
exports exact native B-reps; sheet pose is `flat` by default or `formed`.
The Formed/Flat Studio display choice does not edit the document. See
[surfacing](SURFACING.md), [sheet metal](SHEET_METAL.md), and
[native Studio controls](NATIVE_STUDIO.md).

The previous custom primitive and mesh features remain available. Native
surfaces and sheets cannot be passed through approximate mesh transform,
Boolean, or sketch-cut operations. Use authored parameters, construction
planes, or assembly instance placements. The native worker and current tests
are C++; no Python runtime or test gate is required. The Windows native build
uses OCCT 8.0.1 from [the pinned SDK](NATIVE_BUILD.md).

## 0.9 exact references, assembly mates and authored sheet graphs

The API currently lists **34 tools**. `reference_inspect{id,reference?,fraction?}`
discovers producer-authored exact face, edge and axis names and resolves their
local frames. `sketch_diagnose{name}` or `sketch_diagnose{sketch}` reports
per-constraint residuals and local rank without changing the document. Use
these names in supported construction-plane supports and assembly mates;
triangle and B-rep enumeration indexes are not persistent references.

`doc_apply` accepts `assembly.edit{id,fixed:true}` and
`mate.add{a,b,type,reference_a,reference_b,value?,fraction_a?,fraction_b?}`,
`mate.edit{id,...}`, `mate.remove{id}`. The two IDs identify part instances.
They may sit under different groups. A mate across groups moves each immediate
parent and its descendants as one rigid body. A same-parent mate still moves
only the part. Every connected mate set needs a fixed anchor: a fixed endpoint,
or, across groups, a parent group that contains a fixed descendant.
Types are `concentric`, `coincident`, `contact`, signed `distance` in mm,
and `angle` in degrees. Solved instance transforms are derived after feature
recompute; authored placement and value expressions remain in the document.
`assembly_info.diagnostics` reports mate residuals, local rank, remaining
degrees of freedom and unconstrained instances. Invalid or stale references,
unanchored systems and unsatisfied mates fail the full `doc_apply` batch.
See [the mate contract](MECHANICAL.md#grounded-mates).

The version-7 `.ark` package retains fixed instance flags, mate definitions,
native solid programs, supported construction references and sheet authoring
programs. Older document versions remain readable. `native_solid` performs
supported exact primitive and Boolean operations through the C++ OCCT worker.
The newer sheet program keeps a base panel, ordered flange attachments and
panel-owned openings; its formed/flat B-reps and refold checks remain geometric
evidence, with bend trajectory collision and material calibration outside
current validation. `sheet_preview{id,progress}` returns read-only sequential
flat-to-formed panel transforms at progress 0..1 for display; it does not
edit the document, change the exact export pose, or certify a bend trajectory.

The 0.7 live Studio connection remains the way an MCP client works on the
currently open document. A standalone `ark-mcp` process still has its own
document, and session edits still require the current revision for safe batches.

`import_mesh{path,name?,weld_mm?,expected_revision?}` is the 34th tool. It
reads `.stl` or `.obj` once and stores a `mesh_body`. `weld_mm` is optional and
must lie in `(0, 1]`. The file is not linked. Caps are 12288 vertices, 4096
triangles, coordinates within 1e6 mm, a 32 MiB source file, and the existing
4 MiB document limit. OBJ faces with more than three corners are fanned from
the first vertex and are limited to 64 corners. Binary STL is recognized when
the file length is `84 + 50 * count`; otherwise the text form is read.

`doc_apply` also accepts exact `sweep`, `extrude_surface`, `revolve_surface`,
`offset_surface`, and `sew`, plus sampled `mesh_body`, `mesh_weld`,
`mesh_smooth`, `mesh_decimate`, `mesh_offset`, and `mesh_fill`. Exact surface
arguments and examples are in [surfacing](SURFACING.md). A document that
contains one of these features is version 8. Save chooses the document
version. Draft, `linear_pattern`, `circular_pattern`, `mirror`, fillet or
chamfer `native.selection`, loft smoothing or compatibility, or a one-point
loft tip makes that save version 9. Mirror accepts `native.plane` as `XY`,
`XZ`, `YZ`, or a datum name, or `native.face` for one planar face. Selecting
a face, edge, or curve in Studio stores that semantic name and does not add
a further version. Older documents stay at their previous version and remain
readable. The product version stays 0.9.0 and the tool list stays 34.

`assembly.add` and `assembly.edit` accept `pattern` with `type` `linear` or `circular`, an integer `count` from 2 to 32, and `dx`/`dy`/`dz` or `step`/`axis` (`X`, `Y` or `Z`). One node owns the copies. A mate solves that node once and the copies follow. Degrees of freedom count the node once; the BOM counts every copy. `native_solid` `union`, `cut` and `common` accept 2..16 feature inputs and/or `native.instances` naming placed parts, including every pattern copy. Feature inputs stay in part coordinates. Instances are transformed by the solved assembly pose and then booleaned into one exact solid. A round tube is an exact annular sweep along its centerline, initially +X, and can enter a boolean or a concentric mate. A rectangular tube is an exact hollow sweep along the same centerline, initially +X, with width along the section normal and height along the binormal, and can enter a boolean. The display mesh remains the tessellation. Two-input booleans stay at the previous document version. Instance patterns, `native.instances`, or a union of more than two exact solids save as document version 10. `native.op` `shell` and a loft with `native.guides` save as document version 11. `assembly_info` returns `copy` on each instance. Its optional `candidate` probes a mate, including residuals and the two frames, without saving it or changing the revision. `interference` compares display meshes and stays the cheap screen; the default triangle budget is 8000 and `interference:true` raises it to 120000. `clearance` is the signed exact distance between two B-reps, and `measure` is a read-only distance and angle between two semantic frames. Neither writes the document. `feature.edit`, `feature.remove`, `assembly.edit` and `assembly.remove` accept a unique name; `assembly.edit` `parent` does too, and parent 0 stays the root. `mate.edit` and `mate.remove` keep numeric mate ids, or omit `id` and select the unique mate by endpoints `a` and `b`. `snapshot` and the mesh tools accept that node name plus `copy`. A smooth loft publishes `side.<entity>` with status `merged` when OCCT fuses the authored sides.

`doc_info` accepts `detail` `summary` or `design`. Design returns parameter values, feature volume and bounds, sketch entity kinds, and assembly copy counts, without expressions, native payloads or meshes. A revision conflict says to read `doc_info` with detail design. `sketch.patch` changes one sketch coordinate. `sketch_inspect` with `entity` returns that entity's length, bbox and tangents. `reference_inspect` accepts a sketch and resolves `point.<name>`, or `find` returns the unique planar face for a normal. `native_inspect` `point` returns a signed distance, negative inside one solid. A point datum, `on_curve` or `on_face` support, a sketch arc or wire, a tangent constraint, and `curve`, `intersection_curve` or `bridge` save as document version 12. Loft guides and sweep paths accept a curve feature name. A bridge of continuity 1 uses an end derivative equal to the unit tangent times the chord length. Continuity 2 matches those tangents and uses zero end curvature. The tool count stays 34 and the product version stays 0.9.0. `feature.edit` accepts `suppressed`. A true value leaves that feature out of recomputation and keeps its history and parameters. Sketch entities also include `project` `{feature,reference}` and `offset` `{source,distance}`. `feature` is an id or a unique name, and the saved document stores the id. `distance` is signed millimetres, positive to the left of the directed curve. Both entities are driven, and `sketch.patch` rejects them. `canvas.set` `{name,path,width,height,plane?,opacity?}` and `canvas.remove` `{name}` store a photo rectangle on a plane. The kernel does not decode the image. A plane canvas, a suppressed feature, or a projected or offset sketch entity saves as document version 13. The tool count stays 34 and the product version stays 0.9.0. `doc_apply` accepts `sketch.fillet` `{name,entity,corner,radius}` and `sketch.chamfer` `{name,entity,corner,distance}` on one wire. The corner is a vertex index. The radius or distance stays an expression, and the sharp wire remains the authority, so `sketch.patch` retrims it. A fillet is the minor arc on the turn side. Replacing the same entity and corner updates the size and the kind. A mate may join part instances in different groups. Across groups, the solve moves each immediate parent and its descendants as one rigid body about that group's authored origin. A same-parent mate still moves only the part. Optional `p.density` is grams per cubic centimetre, positive, finite, and at most 100. It does not change the document version. `native_inspect` then adds local `mass_g` and `center_mm`. `assembly_info` adds the assembly mass and the world center when every visible placed part has a density, and sets `mass_checked` false with `mass_reason` when one is omitted. A sketch fillet, a sketch chamfer, or a mate across groups saves as document version 14. The tool count stays 34 and the product version stays 0.9.0.

Analytic cylinder, cone and torus meshes run along +Y. A `native_solid` cylinder, cone or torus uses local +Z. Promoting an analytic cylinder, cone or torus applies Rx(-90 degrees), so the exact B-rep axis becomes +Z. Mate references `Z`, `origin` and `XY` use the instance frame +Z. Semantic reference `axis` is the exact B-rep cylinder axis. The analytic mesh axis is not retargeted. A round tube exact axis is its centerline and starts along +X. A rectangular tube exact axis is that same centerline: width follows the section normal and height follows the binormal. The tube display mesh uses that same centerline. Studio toolbar booleans stay refused while an assembly is active.

Mesh features store no B-rep. The kernel does not decode a photo. A prismatic
outline from a photo is a sketch wire followed by an extrude. A freeform photo
is a `mesh_body` whose `positions` are a row-major point grid and whose
`grid` is `{nu,nv}`, with `nu` and `nv` integers from 2 to 256 and `nu*nv`
points. The kernel builds the triangles. `grid` and `indices` together are
rejected. `mesh_body` keeps the authored `mesh:{positions,indices}` object
when the triangles are explicit; positions are three numbers per vertex and
indices are three per triangle, with the same vertex and triangle caps as
import. A grid document stores `grid` beside `positions` and remains version 8. Evaluation bakes f32 coordinates and writes a report onto the body.
`mesh_inspect{id}` attaches that report as `mesh_model` when the feature is a
mesh operation. `native_inspect` on a mesh feature fails and tells the caller
to use `mesh_inspect`.

`mesh_weld` takes one input. `p.epsilon` defaults to 1e-4 mm and must be in
`(0, 1]`. It may run on a non-manifold mesh. A result with no triangles is
rejected. `mesh_smooth` is a uniform Laplacian: `p.iterations` is an integer
from 1 to 64, `p.lambda` defaults to 0.5 in `(0, 1]`, and `p.pin_boundary`
defaults to 1. `mesh_decimate` collapses interior edges to their midpoint using
quadric error. `p.ratio` is required and lies in `[0.05, 1]`. Boundary edges
are not collapsed. If the input is closed and oriented, the output stays
closed; if it is open, the boundary-edge count stays the same. When the ratio
cannot be met without breaking that rule, the feature still succeeds and
`target_reached` is false. `mesh_offset` moves vertices along normals by
`p.distance`, with absolute value in `(1e-9, 1e4]`. A closed oriented input is
oriented outward first, so a positive distance grows it. The report's
`offset_model` is `vertex_normal`: corners of a box move by about
`distance/sqrt(3)` per axis, not by a constant shell. `mesh_fill` closes each
boundary loop that lies within 1e-4 mm of one plane, using an ear clip of 3 to
256 vertices. Non-planar loops fail and name the measured deviation;
`surface_fill` is the exact operation for a curved boundary.

Smooth, decimate, offset, and fill reject non-manifold, inconsistent,
degenerate, or non-finite input, and they accept at most 12000 triangles.
Every mesh report sets `mesh_authoritative` true, `exact_geometry` false, and
`self_intersections_checked` false. `sampled_from_brep` is true only after an
explicit `p.sample` of 1. Legacy analytic bodies have no B-rep and can enter a
mesh operation without that flag. Call `agent_onboard{topic:"mesh"}` for the
current example.
