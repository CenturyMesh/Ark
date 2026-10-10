# ARK 005B — Agent-Ready Kernel

**Parametric CAD for people and agents.**

Public iteration **005B** uses runtime **0.9.6**. The three digits are globally
sequential; A–G identifies the roadmap phase, not the month. See the mandatory
[development rules](AGENTS.md), the [35-milestone roadmap](docs/ROADMAP.md), and
the [open-source / €200 perpetual license analysis](docs/COMMERCIAL_STRATEGY.md).
`release.json` is the single source of release identity.

005B adds exact three-point/tangent arcs in 2D and spatial curves, a feature-tree
scrollbar, linked tube-end cuts, selective design inspection, and explicit
task/goal completion reporting. [Complex modelling audit](docs/COMPLEX_MODELING_AUDIT_005B.md).

004B adds direct transparency controls, an adaptive grid and joint-aware assembly
manipulation with persistent poses. Agents can inspect, preview and apply the same
movements. [Assembly manipulation](docs/ASSEMBLY_MANIPULATION.md).
Provider connectors and external MCP configurations cover Claude, Grok, Ollama
and OpenCode alongside the existing ChatGPT/Codex routes. [Connectors](docs/AI_CONNECTORS.md).

003D adds bounded automatic recovery from eligible HTTP service failures before
a model response is accepted. Previously completed CAD operations are retained;
the retry status is visible and can be cancelled. [Recovery scope](docs/iterations/003D.md).

002B restores concept-first agent workflows, coherent atomic batches and scoped
preview commits, with measured Provider/tools activity. Sketch adds linked exact
2D projection and spatial Include 3D curves. Frame editing also works through an
assembly instance. [Diagnosis and recovery](docs/AGENT_RUNTIME_RECOVERY.md).

Every ribbon exposes **All tools**, a searchable category gallery backed by the
same commands as the palette. **Library** searches and inserts reusable STEP
components. The native SQLite/FTS5 catalog retains original source hashes,
declared dimensions, provenance and exact geometry evidence; public KiCad source
discovery and direct HTTPS acquisition feed the same validation path.
[Library architecture](docs/COMPONENT_LIBRARY_ARCHITECTURE.md).

ARK's **Assistant > Team** defaults to **Focused**: the engineer authors the
design and delegates concrete work to a designer or reviewer, one conversation
at a time. **Parallel** remains available for up to eight agents. Readable activity
cards show independent model settings, measured token usage and tool feedback.
Document writes remain serialized and revision checked.
[Team controls](docs/MULTI_AGENT_STUDIO.md) and
[C++ module architecture](docs/AGENT_TEAM_ARCHITECTURE.md).

The view cube stays at the upper left of the viewport; shaded Cartesian arrows
and X/Y/Z letters sit at the lower right, without a background sphere or disc. **ARK updates** prepares verified packages
for the next launch, with rollback. The future website delivery channel is not
yet connected. [Self-update preparation](docs/UPDATES.md).

`manufacturing_inspect` separates injection molding, FDM additive manufacturing
and machining intent from verified geometry and unresolved process checks.
[Manufacturing contract](docs/MANUFACTURING_INTENT.md).

ARK has viewport/tree context menus, reversible **Spotlight** visibility,
separate navigation and playback rows, and improved face/curved-edge picking.
[Interaction guide](docs/STUDIO_INTERACTION.md).
The engineering update adds verified edit conditions, reusable preview
candidates, exact rigid-body mass/CG/inertia and load reports, and bounded
multicore feature evaluation. A Physics ribbon and 48 generated icons accompany
the new tools. [Contracts and examples](docs/ENGINEERING_AGENT_V2.md).
**Continue with ChatGPT** connects eligible subscriptions through the browser;
API-key setup remains available under Advanced. [Assistant setup](docs/ASSISTANT.md).

The [KITE suspension showcase](examples/kite-17-suspension.ark) adds four
double-wishbone corners, knuckles, coilovers, steering links and spinning wheels.
Choose **Frames > Corner** to spotlight and animate a mechanism. The travel,
rack and wheel controls run a checked kinematic preview with ideal spring force.
[Suspension contract and limits](docs/SUSPENSION.md).

[KITE 17](examples/kite-17-showcase.ark) is a complete visual showcase of a
fold-built coupe: tubular subframe, nine sheet definitions, wheels and glazing.
The **Sheet Metal** ribbon switches the assembly between formed, unfolded and
animated views, with nominal tube stock on a separate display. Drawing token
receipts now persist in the `.ark` file. [Showcase guide](docs/KITE_SHOWCASE.md).

The engineering workspace adds a grouped command ribbon, readable orientation
cube/compass and responsive kernel loading overlay. Construction Frames supports
round/rectangular bent members, cutlists and supported cope joints; PCB and
cable routing add checked layouts, netlists and exact obstacle checks.
[Open the integrated vehicle](examples/vehicle-engineering.ark),
[read the module contracts](docs/ENGINEERING_MODULES.md),
[export formats](docs/EXCHANGE.md), or
[CPU/GPU execution details](docs/COMPUTE_ARCHITECTURE.md).

Studio now supports persistent colors, material properties and image textures,
metallic/roughness lighting, transparent bodies, and configurable contact,
ambient and horizon occlusion. The Material and Physics controls expose
appearance, density, mass, weight and center of mass. The in-app assistant
shows provider-reported token usage, with unavailable/partial states when the
provider omits it. See [materials and rendering](docs/RENDER_MATERIALS.md).

![ARK 0.9 parametric drone assembly](docs/drone-hull.png)

ARK is a C++ parametric CAD workspace with a Windows Studio editor, a headless
modeling library, and a local MCP server. Studio and MCP operate on the same
document, feature history, undo stack, and revision when connected. ARK is an
engineering prototype; its native STEP and sheet domains are deliberately
bounded.

## Build and run on Windows

Install CMake 3.21+ and Visual Studio 2022 Build Tools with the x64 C++ and
Windows SDK components. The default build uses MSVC and Open CASCADE Technology
(OCCT) 8.0.1. The first run downloads, verifies, builds, and installs the OCCT
SDK under ignored `out/deps/occt`; subsequent builds reuse it.

```powershell
.\scripts\build.ps1 -Test
.\start-studio.ps1
.\start-studio.ps1 -Document .\examples\freeform-plate.ark
# Force a tested rebuild before opening Studio:
.\start-studio.ps1 -Rebuild
```

The native executables are in `out/native-release/Release`. `start-studio.ps1` accepts
`-BuildDirectory` for another MSVC build tree and stops if its build or tests
fail. The prior `out/sketch` MinGW binaries are left in place; they are not the
current native build. [SDK source, license, and paths](docs/NATIVE_BUILD.md).

The C++ core can be configured without OCCT for portability checks:

```sh
cmake -S . -B out/core -DARK_WITH_OCCT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build out/core --config Release --parallel 2
ctest --test-dir out/core -C Release --output-on-failure
```

This core configuration does not provide native STEP modeling. The native OCCT
job in CI runs on Windows; this repository does not claim a proven Linux native
OCCT build. GitHub Actions execution may remain unavailable while the account
billing restriction recorded in [validation](docs/VALIDATION.md) persists.

## Work in Studio

Use the floating dock or **Commands** (`Ctrl+K`) for sketches, primitives,
assemblies, tubes, native surfaces, and sheet metal. The view toolbar and the
view cube select orbit, pan, zoom, fit, and the orthographic face views; `1`
through `7` jump to front, back, top, bottom, right, left, and isometric.
Ambient occlusion and visible edges are display-only. Dragging orbits, Shift+drag
pans, the wheel zooms, and `F` frames the model. `Ctrl+N`, `Ctrl+O`, `Ctrl+S`,
`Ctrl+Z`, and `Ctrl+Y` manage the document and history.

Native commands create exact primitives and supported Boolean, hole, fillet,
chamfer, draft, linear pattern, circular pattern, mirror, and rigid transform
features. In Studio, Face, Edge, and Curve select semantic names for those
features. Mirror uses a principal plane, a datum, or one planar face. They
also loft saved sketch sections, including a point tip,
fill closed sketches, sweep a section along a path, extrude or revolve a
sketch as a surface, offset or sew open surfaces, edit a NURBS pole grid, and
thicken an open surface. The Mesh commands weld, smooth, decimate, offset, and
fill planar holes, and they import an STL or OBJ snapshot.
Assembly instances can be grounded and joined by named-reference mates;
the inspector reports residuals and remaining free dimensions. The sheet
program stores a base panel, ordered flanges, and panel-owned openings.
**Formed** and **Flat** select display poses without modifying the document;
`sheet_preview` returns read-only intermediate display transforms. Sheet STEP
export asks for a pose and defaults to the flat blank.
See [Studio controls](docs/NATIVE_STUDIO.md) and [surfacing contracts](docs/SURFACING.md).

Source STEP tools preserve supported original geometry and openings. Tube
sources keep their separate straight/folded hinge preview. Sheet sources are
read-only and are verified against their original STEP and a refolded native
B-rep. Imported sheet support is limited to one uniform-thickness panel or
two rigid flanges joined by one complete straight cylindrical bend. The
importer rejects ambiguous and unsupported shapes; it does not silently
approximate them. See [source tube](docs/SOURCE_STEP.md) and
[sheet metal](docs/SHEET_METAL.md).

## Connect an agent

Start `out/native-release/Release/ark-mcp.exe` as a local MCP stdio server. Each
standalone process owns one document. To operate the open Studio document,
connect the MCP process to Studio's displayed endpoint as described in
[Codex and live Studio](docs/CODEX_CONNECT.md).

```json
{
  "mcpServers": {
    "ark": {
      "command": "C:/path/to/ARK-Point/out/native-release/Release/ark-mcp.exe",
      "args": []
    }
  }
}
```

Call `agent_onboard` first for the current revision, supported operations,
and examples. ARK exposes 64 MCP tools, including the [agent geometry and numerical tools](docs/AGENT_AUTOMATION.md), `kernel_info`,
`reference_inspect`, `sketch_diagnose`, `sheet_preview`, `import_sheet_step`, and `import_mesh`. `doc_apply` batches
edits atomically and rejects stale revisions. The [agent protocol](docs/AGENTS.md)
describes the command and validation contracts. The in-app assistant uses a
separately configured Responses-compatible service; modeling, Studio, and MCP
do not require API credentials.

## Geometry boundary

The earlier custom C++ primitive, expression-surface, mesh, and assembly paths
remain available. OCCT 8.0.1 supplies exact B-reps for supported native
solids, lofting, filling, sweeping, surface extrusion and revolution, offset,
sewing, NURBS, thickening, sheets, source import, and STEP
operations. A viewport mesh is a tessellated preview, not the STEP authority.
Native exact bodies use supported native operations and cannot be silently
routed through legacy approximate mesh operations. Sampling one into
`mesh_weld`, `mesh_smooth`, `mesh_decimate`, `mesh_offset`, or `mesh_fill`
requires `p.sample` of 1, and that result stays a mesh.

Sheet flat length uses the caller's declared K factor. It does not infer a
material calibration or certify forming. A non-midwall K factor can produce a
modeled formed/flat volume difference, reported explicitly. Native B-rep
validity, source refold checks, and source hashes are geometric evidence, not
manufacturing qualification. General exact B-rep support for every legacy
feature, arbitrary multi-bend sheet unfolding, and manufacturing constraint
certification are outside this release.

All current production code and test programs are C++; no Python runtime or
test gate is required. Run `scripts/build.ps1 -Test` for the native CTest set.
The independent stdio check is in CTest. The live-Studio process check is an
explicit C++ opt-in because hosted CI may not provide the OpenGL 3.3 context
needed by Studio; a headless CTest result does not establish UI rendering.
See
[validation](docs/VALIDATION.md), [file formats](docs/FORMATS.md), and the
[engineering assessment](docs/ASSESSMENT.md).

## Reproducible 0.9 demonstrations

Open the authored drone directly:

```powershell
.\start-studio.ps1 -Document .\examples\drone-hull.ark
```

It contains a spline hull, real electronics holes and strap slots, shared motor
arms, four motors and a lofted/thickened inspection canopy. The canopy is shown
lifted; this is a modeling demonstration, not a qualified flight design. The
[replay](examples/drone-hull.arkops) uses the same public agent commands as Studio.
The [wider open variant](examples/drone-hull-wide-open.ark) changes width and hole
pitch and hides the canopy so the internal geometry is visible.

[Hull STEP](examples/drone-hull.step) and [canopy STEP](examples/drone-canopy.step)
are exact local part definitions; they are not a STEP assembly of every instance.
Their readback differences and test evidence are recorded in
[verification-0.9.json](docs/verification-0.9.json).

The same agent commands model a six-axis articulated arm:

```powershell
.\start-studio.ps1 -Document .\examples\six-axis-robot.ark
```

![Six-axis robot arm](docs/six-axis-robot.png)

`six-axis-robot.ark` holds 14 shared part definitions and 23 assembly nodes. The
kinematic chain is nested assembly groups: J1 yaws about +Y, J2/J3/J5 pitch about
+Z, and J4/J6 roll about the local +X link axis. Each joint angle is the `j1`..`j6`
parameter, so editing one re-poses the whole arm; `upper_len`, `fore_len` and
`shoulder_h` re-dimension it. The links are hollow round tubes and the castings
are 360-degree revolves of closed sketch profiles. Replaying
[the ops file](examples/six-axis-robot.arkops) from an empty document reproduces
the saved package byte for byte. Assembly groups place instances; they do not
fuse bodies, check interference or certify joint limits.


```powershell
.\start-studio.ps1 -Document .\examples\sheet-flange-tree.ark
```

The sheet sample has a base, three signed flanges and two panel-owned openings.
Use **Flat / Formed / Play**, or edit its flange and opening forms. Its exact
[flat](examples/sheet-flange-tree-flat.step) and
[formed](examples/sheet-flange-tree-formed.step) STEP files are included.

![Wider hull with the canopy hidden](docs/drone-hull-open.png)
