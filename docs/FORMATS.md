# ARK file formats, versions 1 through 5

## `.ark`

UTF-8 JSON carrying editable design intent, not a triangle dump. `.arkjson` is an accepted legacy extension for the same payload.

```json
{
  "format":"ark",
  "version":1,
  "units":"mm",
  "next_feature_id":2,
  "params":[{"name":"width","expr":"60"}],
  "sketches":[],
  "features":[{
    "id":1,"type":"box","name":"Base","op":0,
    "inputs":[],"p":{"dx":"width","dy":"30","dz":"8"}
  }]
}
```

Feature parameters are expression **strings** in saved files. Unknown document versions/units, duplicate IDs, invalid references and unresolved parameters are rejected. Legacy version-1 documents without `format`, `units` or `next_feature_id` can still load. Features are ordered, reference earlier features, and own stable integer IDs no greater than JSON's exact integer range. `next_feature_id` prevents ID reuse after deletion.

Named sketches contain `name`, `entities` and `constraints`. Entity kinds are `point` (two DOFs), `line` (four DOFs) and `circle` (three DOFs). Constraint types are discoverable through the source's `constraintTypeName` contract.

Serialization sorts each feature's parameter keys for deterministic diffs. Save writes a sibling temporary file and then replaces the destination. Separate sessions saving the same path are last-writer-wins. Loaded content is parsed into scratch state; the agent API commits only after successful geometry evaluation.

Camera, UI layout, undo stack, cached meshes and session revision are not persisted. File version and session revision are separate concepts. There is no compressed/binary `.ark` container in this version.

## `.arkops`

UTF-8 newline-delimited JSON. Each line is a Studio command envelope containing `tool` and `arguments`; the tool contract is shared with MCP. A failed line aborts replay; previous successful lines remain committed within that process. To make a set of changes atomic, put those changes in **one `doc_apply` batch**.

```powershell
.\out\build\ark-studio.exe --load examples/harmonic-ring.ark --script examples/refine.arkops
```

`refine.arkops` expects the harmonic-ring document. It edits parameters and can be replayed by any client that parses a line and calls the matching MCP tool. It is not executable C++ or a general scripting language.

## Exchange

STL: binary triangle export. OBJ: text triangle export. PNG: raster visual feedback. Only terminal feature outputs are exported by the agent/Studio interface, unless a feature ID is explicitly selected. These exchange files do not preserve the parametric history.

## Version 2: assemblies and tube programs

Documents using tubes, assembly nodes or an assembly ID allocator are written with `version:2`. Version 1 remains supported for legacy part documents. Version 2 adds `assembly`, `next_assembly_id`, and each tube feature's `bends` array. Assembly node fields are `id`, `parent`, `feature`, `name`, `visible` and expression map `p`. A group uses `feature:0`; a root uses `parent:0`. Bend entries store expression strings for `feed`, `radius`, `angle` and `roll`.

See [mechanical operations](MECHANICAL.md) for conventions, validation and examples. The original 0.2 reader rejects version 2 rather than silently losing assemblies or bend programs.

## Version 3: editable laser tube

`notched_tube` adds a `notches` array of `{station,angle,face}` expression strings, alongside stock dimensions in the feature's `p` object. Zero points are supported. Saving preserves intent and stock expressions; the animated pose is not serialized. The source document and all exported STEP geometry stay independent of display progress. See [BEND_WORKSPACE.md](BEND_WORKSPACE.md).

## Version 4: source-preserving tube package

A `source_tube` feature carries its `source` package: rigid preview meshes, finite hinge geometry, source/flat projection evidence, original opening coordinates and an embedded exact straight STEP payload with SHA-256. No manufacturing authority is implied. The preview and export remain available after the original cache moves; if the original source file is present but changed, export requires a fresh import. Geometry changes go through `import_step`, not generic feature-parameter edits.


## Version 5: construction planes and expression-driven sketches

Documents containing planes, sketches or `sketch_cut` use version 5. The reader
continues to accept versions 1 through 4, including source tube packages. Planes store
`name`, `parent`, and an expression map `p` (offset and rotation). Sketches add
`plane` and `tolerance`; each entity can have a unique `name` and `construction`
flag. Spline entities store `closed`, `tension` and flattened XY interpolation
anchors in `dof`. String DOFs are parameter expressions; numeric DOFs are solver
variables. Constraint `val` and `val2` can likewise contain expressions.

Expression text is preserved through save/load, undo and recomputation. Resolved
coordinates and diagnostic rank are runtime evidence, not a substitute for the
stored design intent. [Full contracts and examples](PARAMETRIC_SKETCH.md).
