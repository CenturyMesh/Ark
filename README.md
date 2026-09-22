# ARK

A C++ geometry workspace designed to be operated by people and agents.

![ARK Studio](docs/laser-straight.png)

ARK combines a native Windows/OpenGL editor, a headless geometry library and a local MCP server. All new editor commands and MCP tools use the same `AgentSession` implementation. It is an **early engineering prototype**, with an existing custom kernel extended and corrected here, not yet an industrial CAD replacement.

## Run

Windows requirements: CMake 3.20+, Ninja and a C++20 compiler on PATH. The included PowerShell build uses MinGW-w64; MSVC can build through CMake from a Developer terminal.

```powershell
.\scripts\build.ps1 -Test
.\start-studio.ps1
# Open a saved document:
.\start-studio.ps1 -Document .\examples\harmonic-ring.ark
```

The kernel and MCP have portable CMake targets for Linux; CI execution is currently blocked by the GitHub account billing limit:

```sh
cmake -S . -B out/source -DCMAKE_BUILD_TYPE=Release
cmake --build out/source --parallel 4
ctest --test-dir out/source --output-on-failure
```

Targets: `ark` static library, `ark-mcp`, `ark-cli` (legacy demonstration), `ark-tests`, `ark-agent-tests`, and `ark-studio` on Windows. No JavaScript runtime, web server, cloud account or model subscription is required by ARK itself. MinGW executables link their runtime statically.

## Work in Studio

- Orbit by dragging, pan with Shift+drag, zoom with the wheel; `F` frames the model.
- Click an output in the tree or viewport to inspect it. Mouse-wheel over the tree scrolls history.
- Click a named parameter to edit its expression. The example's `radius`, `tube`, `waves` and `amplitude` change the harmonic surface.
- Use the toolbar for primitives, extrude, revolve, pattern and mesh booleans. Boolean buttons operate on the last two terminal outputs.
- `Ctrl+K` opens the JSON command editor; `Ctrl+Enter` submits a multiline command.
- `Ctrl+S` saves, `Ctrl+O` opens, `Ctrl+Z` undoes, `Ctrl+Y` redoes, `W` toggles wireframe, Delete removes the selected output.

## Agents

Start `out/source/ark-mcp.exe` as a local MCP stdio server from your MCP client. The interface is model independent: an Astra or other agent host can drive it **if that host can invoke MCP tools**. ARK does not embed a language model or provide a natural-language chat service.

```json
{
  "mcpServers": {
    "ark": {
      "command": "C:/path/to/Ark/out/source/ark-mcp.exe",
      "args": []
    }
  }
}
```

This is a generic MCP configuration example; the host's configuration format may differ. See [agent protocol](docs/AGENTS.md) for the tool contract and examples. Studio and a separately launched MCP process each own an independent session. Use `.ark` files to hand work between them; live cross-process synchronization is not implemented.

## STEP source with its original holes (0.6)

Use **Import STEP** to load a supported planar tube, preserving its actual circular/polygonal openings and cutouts. The source can be viewed formed or straight, animated through its measured finite-radius hinges, and exported as a straight STEP with the original geometry. Clicking an opening focuses the real cut. [Projection method, evidence and supported domain](docs/SOURCE_STEP.md).

The imported source stays read-only; it is not replaced with an approximate empty tube. Geometric source verification is separate from physical manufacturing qualification.

## Interactive laser tube (0.5)

```powershell
.\start-studio.ps1 -Document examples/laser-tube.ark
```

Choose **Laser tube**, define stock and **Add bend** points, then use **Play**, **Straight**, **Folded** or the slider to inspect the motion. **Save** keeps the editable design; **Export straight STEP** always writes the straight notched tube, regardless of the animation pose. [Controls, geometry contract and backend setup](docs/BEND_WORKSPACE.md).

## Neutral desktop UI and notch STEP preview (0.4)

Studio now uses a neutral graphite palette, satin steel shading, a geometric ARK logo and an icon ribbon. Primitive dimensions and placements use native property dialogs; expressions and agent commands have dedicated editors. The kernel stays independent of these presentation controls. The interface remains native Win32/OpenGL. A future WPF presentation host can interoperate with native C++, as described in [Microsoft documentation](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/wpf-and-win32-interoperation); this release does not migrate the application to WPF.

The new C++ `notch_plan` tool can produce a straight rectangular tube cut plan with a retained hinge wall. An optional OCCT bridge exports and checks the actual STEP solid. See [workflow, example STEP and mathematical evidence](docs/NOTCH_WORKFLOW.md). This is an ideal geometric preview; automatic reconstruction/refolding of the supplied STEP and physical qualification are not yet complete.

## Assemblies, tube bending and GPU compute (0.3)

```powershell
.\start-studio.ps1 -Document examples/welded-support.ark
.\start-studio.ps1 -Document examples/spatial-tube.ark
```

Assemblies have shared part definitions, nested groups, independent rigid placements and BOM quantities. Studio shares GPU mesh buffers and local BVHs between instances. Use **Assembly**, **Duplicate** and **Placement** to organize parts. Tube programs support round and rectangular hollow sections, multiple bends and plane rotations. **Tube** creates a bend; **Edit tube program** changes its section and bend sequence.

An optional CUDA backend accelerates large packed position transforms, while analytic geometry and placement calculations stay in f64. It loads the installed NVIDIA driver and NVRTC runtime dynamically; CUDA headers must be available when configuring the build and `CUDA_PATH` must locate NVRTC at runtime. No nvcc build or GPU driver change is required. CPU builds and fallback remain supported. [Scope, measurements and MeshARC assessment](docs/MECHANICAL.md).

## What is implemented

| Layer | Current capability |
| --- | --- |
| Math | f64 vectors, matrices, quaternions, arithmetic/trigonometric expressions, named parameter dependency resolution |
| Geometry | Analytic primitive faces, NURBS curve/surface evaluation, extrude/revolve, mesh BSP boolean operations, transforms and linear patterns |
| Complex surfaces | `x(u,v)`, `y(u,v)`, `z(u,v)` with named expressions, indexed tessellation and validated periodic seams |
| Mesh reasoning | Cached geometry and topology reports, bounds, area, connected components, oriented-edge checks, BVH ray intersection and explicit buffer retrieval |
| Agent transactions | Batches of up to 128 commands, one geometry recompute, rollback on failure, expected revision, stable feature IDs, 32 undo steps |
| Assemblies | Shared part definitions, nested groups, rigid instance placement, visibility and BOM quantities |
| Tube programs | Round/rectangular hollow profiles, planar/spatial bends, geometric developed length and bend tables |
| Compute | GPU model transforms with shared display buffers; optional CUDA packed transforms and reproducible CPU/GPU benchmark |
| Files | `.ark` versioned JSON document; `.arkjson` compatibility; `.arkops` line-by-line command replay; STL/OBJ export; PNG snapshots |
| Studio | Native GPU viewport, selection, model tree, expression editing, mesh inspector, command console, open/save and undo/redo |

See [file formats](docs/FORMATS.md), [assessment and roadmap](docs/ASSESSMENT.md), and [validation](docs/VALIDATION.md).

## Boundaries

Boolean outputs and expression surfaces are meshes. Closed oriented edges do not establish freedom from self-intersection or a manufacturing-valid solid. The existing B-rep does not yet implement a complete shared edge/vertex topology, robust exact boolean operations or persistent face naming. Primitive tessellation does not consistently honor all `TessOptions`. STL/OBJ import, STEP exchange, fillets, robust remeshing, assembly mate solving and a production constraint solver remain future work.

Trace and Stage are possible future adapters. This project currently has no runtime dependency on either.
