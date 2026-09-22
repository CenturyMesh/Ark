# ARK file formats, version 1

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
