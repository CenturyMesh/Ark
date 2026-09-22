# ARK agent protocol

The C++ MCP host reads one JSON-RPC message per line on stdin and writes protocol responses only to stdout. Tools return both `structuredContent` and its JSON text representation, with `isError` set on failures, following the [MCP tool result specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools). Transport behavior follows [MCP stdio](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

Initialize with protocol `2025-11-25`, `2025-06-18` or `2024-11-05`. Each process owns one document. The API is synchronous; a long operation occupies that session. Applications must not call a single `AgentSession` concurrently.

## Recommended modeling loop

1. Call `doc_get` once to acquire the current document and revision.
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

Use `created_ids` returned by a batch as inputs to later features. IDs are monotonically allocated within a document and persisted, including its next-ID counter. Inside a batch, references can target known earlier IDs; symbolic references are not implemented. Features must follow their dependencies in history. Removal of a dependency without also removing/replacing its dependants fails atomically.

## Tools

| Tool | Purpose |
| --- | --- |
| `doc_info`, `doc_get` | Compact summary / full parametric document |
| `doc_apply` | Atomic modeling batch |
| `doc_undo`, `doc_redo` | In-memory transaction history; revision increases |
| `eval_expr` | Finite arithmetic and trig evaluation |
| `mesh_inspect` | Indexed topology, measurements and explicit validation limits |
| `mesh_raycast` | Nearest triangle hit, normalized direction, distance in mm |
| `mesh_get` | Positions and triangle indices, explicitly requested |
| `doc_save`, `doc_load` | Native document persistence |
| `export_mesh` | STL/OBJ of terminal outputs or a selected feature ID |
| `snapshot` | Software-rendered PNG of terminal outputs or selected ID |

The process has local filesystem permissions of the host. Save/export tools write the supplied paths. File saves replace existing documents; there is no cross-process file locking. MCP sessions are local, not a remote HTTP service.

## Numerical contract

Document lengths use millimetres. `sin`, `cos`, `tan`, `asin`, `acos`, `atan2` operate in radians; `deg(x)` converts degrees to radians. Transforms' `rx/ry/rz` and revolve's `angle` use degrees. Expressions support `+ - * / ^`, parentheses, scientific notation, `sqrt`, `abs`, `min`, `max`, `pow`, `floor`, `ceil`, `pi` and `e`. Use explicit parentheses around negative bases when exponentiating.

Surface coordinates use `u` and `v`; defaults are domains `[0,2*pi]`, with `nu=96`, `nv=48` subdivisions. Resolution is limited to 3..256 per axis. `wrap_u=1` and `wrap_v=1` request shared seam indices; sampled endpoints must agree within 1e-6 mm. This is a sampled seam check, not a symbolic continuity proof. Mesh buffers use f32, while expressions and analytic geometry use f64.

Reports use indexed connectivity without silently welding/healing. `closed_oriented_edges=true` only means the implemented edge, degeneracy and finite-data checks passed. `solid_certified=false` and `self_intersections_checked=false` remain explicit. Signed volume is provided only for meshes with closed oriented edges and is not a certified physical volume. Triangle IDs belong to one tessellation; do not retain them across edits.

## Capacity and performance

Documents are limited to 4 MiB on load, 1024 features, 256 parameters and 64 sketches. A sketch accepts 128 entities and 256 constraints. JSON nesting is limited to 64; mathematical expressions to 2048 characters. These are request limits, not a bound on all possible boolean or solver runtime. There is no cancellation or worker isolation yet.

Mesh buffers, reports and BVHs are cached per session revision and selected feature ID. One geometry recompute handles the whole batch. History stores up to 32 serialized document states; evaluated meshes are not duplicated in undo history. The old hand-written SIMD routines are retained as source archaeology but are disabled until ABI/numerical parity and profiling justify them.
