# ARK 0.9 operator and agent workflow

This page records the implementation boundary for the current native CAD
iteration. Commands in Studio and MCP operate on the same document through
`doc_apply`; the live Studio bridge shares the open document and revision.
The standalone MCP host still owns a separate document. Use `expected_revision`
for an edit assembled from earlier inspection, and read the returned IDs before
building further features.

## Launch

On Windows, `powershell -File scripts/build.ps1 -Test` builds the 0.9 MSVC
Release programs and runs CTest. `powershell -File start-studio.ps1` opens
Studio; `powershell -File start-studio.ps1 -Rebuild` performs its tested
rebuild first. In Studio, use **Commands** (`Ctrl+K`) for visual modeling
actions or the displayed live MCP endpoint for an agent connected to that
open document. `out/native-release/Release/ark-mcp.exe` without
`--connect` starts an independent document.

## From sketch to exact solid

1. Define parameters, a construction plane and named sketch entities. A plane
   may use an authored parent frame or a supported exact semantic reference.
2. Run `sketch_diagnose{name}` or submit an uncommitted candidate. Check each
   residual, local rank and remaining DOFs; this inspection changes no history.
3. Add an exact `native_solid` primitive or sketch extrusion. Supported
   exact Boolean, hole and edge finishing operations consume exact B-reps
   through the C++ OCCT worker. Inspect the result with `native_inspect`.
4. Use `reference_inspect{id}` to discover producer-authored face, edge and
   axis names. Inspect a selected reference frame before driving a datum or
   mate. An edit that deletes, splits or makes a referenced item ambiguous
   fails the batch. Mesh triangles remain display and query evidence only.

Supported analytic legacy primitives can materialize an exact B-rep for a
native downstream operation. Other sampled mesh-only paths remain separate.
Do not treat a visually matching tessellation as proof that a downstream
operation has an exact B-rep.

## Assemble without losing part intent

1. Keep each part definition in feature history and add assembly instance
   nodes. Place groups with authored transform expressions.
2. Ground at least one part instance in each connected mate set with
   `assembly.edit{id,fixed:true}`.
3. Add `concentric`, `coincident`, `contact`, signed `distance` or
   `angle` mates between direct sibling part instances. The local
   `origin/X/Y/Z/XY` frames and exact semantic references are available.
4. Read `assembly_info` for derived world matrices, BOM quantities and
   diagnostics. Remaining DOFs and unconstrained instances are reported,
   while the original transform and mate expressions stay editable.
5. Change a parameter or part geometry, inspect the regenerated placement,
   then save the version-7 document. Any unsatisfied or stale mate rejects
   the attempted edit atomically.

Studio exposes grounding, mate creation/edit/removal and a mate inspector on
selected instances. The detailed geometric contract and tolerances are in
[MECHANICAL.md](MECHANICAL.md#grounded-mates).
Each mate endpoint selects one local frame. The numerical rank is local to
the solved placement and does not prove a globally unique assembly pose.
Cross-parent mate graphs and moving subassembly groups are outside this
iteration's solver domain.

## Author a sheet as a panel graph

The sheet authoring program stores a base panel, ordered flange attachments
and panel-owned openings. Each flange names an earlier parent and an edge;
its length, width, offset, radius, signed angle and optional K factor remain
expressions. The native worker builds formed and flat B-reps, checks each
piece for positive-volume interference, rejects nonadjacent contact, and
checks the formed shape against an independent neutral-line refold. Read
`native_inspect` for B-rep validity, volume and refold evidence. Imported
source sheets remain read-only and retain their separate source projection
evidence. The authored graph may have several flanges; the source STEP
recognizer still accepts only its bounded flat or one-bend sheet domain.

The geometric checks do not cover the entire bend trajectory, springback,
tool access or calibrated material behavior. The K factor is a declared
design input. The read-only `sheet_preview{id,progress}` returns sequential
flat-to-formed display transforms; it does not change authored geometry or
the exact flat/formed export poses. The current API lists 33 tools.

## Verification and release gates

Keep the following evidence separate when assessing this iteration:

| Evidence | What it establishes |
| --- | --- |
| `sketch_diagnose` | Candidate constraint residuals and local rank, including failures. |
| `reference_inspect` | A named exact producer reference and its current frame. |
| `native_inspect` | Exact OCCT B-rep validity and feature-specific reports. |
| `assembly_info` | Solved placements, BOM and mate residual/free-DOF diagnosis. |
| `mesh_inspect` and Studio | Display tessellation, picking and visual placement. |
| CTest and Studio smoke | Regression and operator control behavior for the tested cases. |

Before claiming the iteration complete, run the native Release build and
focused CTests for solid modeling, semantic references, sketches, mates and
sheet graphs; then run the full CTest suite and the Studio self-test on a
desktop with OpenGL. Keep exact B-rep validity, source projection, mesh
inspection, assembly placement and manufacturing suitability as separate
claims.
