# Assessment and next engineering stages

Historical assessment made against the local source on 2026-09-22. The local folder had C++ sources and old generated artifacts but no Git repository; `CenturyMesh/Ark` was empty. Its proposed work below reflects that date. ARK 0.8 subsequently added a native OCCT 8.0.1 worker, bounded exact surfacing/sheet/STEP routes, and a live Studio/MCP document bridge. See the current [README](../README.md), [surfacing](SURFACING.md), [sheet metal](SHEET_METAL.md), and [validation](VALIDATION.md) before treating an item below as still open.

## Existing foundation

The source already supplied approximately a dozen kernel modules: f64 math, NURBS curves and surfaces, analytic faces, primitive tessellation, profile sweeps, a small dense sketch constraint solver, mesh BSP booleans, feature history, a C ABI, JSON persistence, MCP and a Win32/OpenGL viewer. This was useful prototype code, but comments claiming exact/robust CAD behavior exceeded what the implementation established.

Key problems found and addressed:

- The batched point-transform path reinterpreted aligned f64 `Vec3` storage as packed f32. The corrected path preserves double coordinates; the historical assembly is disabled by the new build.
- The native viewport copied double matrix bytes into a float GPU uniform. UI clip-space coordinates and font atlas orientation also prevented usable rendering. Both geometry and UI are now rendered and checked from the actual executable.
- Software snapshots omitted perspective division. Snapshots now project using homogeneous W.
- The old per-frame status recomputed tessellation to count triangles. The viewport now reuses buffers/counts and uses cached BVHs for picking.
- Unknown functions, invalid domains and unresolved parameter dependencies could become plausible default geometry. Expressions and transactions now report failures; long dependency chains resolve topologically.
- Agent mutations could leave partial state and did not provide compact structured evidence. The shared session provides rollback, revisions, undo/redo, reports, final-output selection and explicit large-buffer retrieval.
- Saved feature parameter order was unstable; deleted maximum IDs could be reused after replay. Canonical key ordering and a persisted allocator address both.
- Build flags included fast-math globally and the build depended on a machine-specific toolchain path. CMake now supplies a portable baseline without fast-math.

## Deliberate first version

The UI remains native C++/Win32/OpenGL. This avoids replacing the existing kernel or adding a second geometry implementation. Agent integration is model independent through MCP. Expression surfaces demonstrate editable freeform geometry today; they are represented as meshes, with analytic/NURBS entities retained in the existing kernel for further development.

The native document now uses an owned binary container with incremental writes and persistent iteration history; explicit JSON exports remain available. See [document storage](DOCUMENT_CONTAINER.md). Agents should reason from parameters, IDs and reports before asking for vertex arrays.

## Work still required for a serious general CAD

1. **Kernel decision:** assess integrating a mature exact B-rep kernel versus extending the custom prototype. Establish source-owned tolerances, shared topology, robust booleans, fillets, STEP exchange and persistent subshape naming. Do not label mesh CSG as an exact solid operation.
2. **Mesh platform:** STL/OBJ/glTF import, robust welding, self-intersection detection, vertex-manifold tests, repair with provenance, remeshing, decimation, spatial distances and signed distance representations. Validate each operation independently.
3. **Mathematics:** compile expressions once to reusable bytecode; adaptive error-controlled surface tessellation; expose NURBS editing, lofts and path sweeps; diagnose constrained/underconstrained/overconstrained sketches.
4. **Execution:** incremental DAG recomputation, typed complete feature schemas, deadlines/cancellation, worker isolation, memory accounting, asynchronous jobs and larger-document benchmarks. Current caches do not make arbitrary complex booleans bounded.
5. **Product:** live shared document service between agent and Studio, richer parameter/feature editing, Unicode-native file dialogs, materials, assemblies and packaging/signing. Current sessions exchange files explicitly.
6. **Interoperability:** stabilize ARK geometry and exchange contracts before adding external adapters; publish each adapter's scope and verification separately.

SIMD, multithreading and native extensions should follow measured bottlenecks and numerical parity tests. Existing code is retained where usable; broad production-performance or manufacturing-validity claims are not supported by this first version.

## 0.3 follow-up

Rigid hierarchical assemblies, shared-part GPU rendering, ideal hollow-tube bending and an optional CUDA packed-transform backend are now implemented. The corresponding scope and measurements are recorded in [MECHANICAL.md](MECHANICAL.md). The remaining assembly work is mate solving and joint/fabrication operations, rather than basic instances or placement.
