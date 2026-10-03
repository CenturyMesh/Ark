# ARK file formats, versions 1 through 9

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
.\out\native\Release\ark-studio.exe --load examples/harmonic-ring.ark --script examples/refine.arkops
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

## Version 6: native OCCT features and source sheets

Documents containing `loft`, `surface_fill`, `bspline_surface`, `thicken`,
`sheet_metal`, or `source_sheet` use version 6. Authored native features store
their `native` specification alongside expression map `p` and input IDs. A
source sheet stores a read-only `source` package that includes source identity,
geometric proof, original openings, and exact formed/flat B-rep payloads. The
display mesh and native evaluation cache are derived and are not feature
history edits. The Formed/Flat UI selection does not change the saved geometry.

The reader accepts versions 1 through 9. Older ARK executables reject a newer
document rather than discarding its native data. Save/load re-evaluates authored
native features with the installed OCCT 8 worker; source packages remain
portable when the original STEP is absent. If the source STEP still exists
but its bytes have changed, native source export requires reimport.

## Version 7: mates, grounded instances, native solids, and sheet graphs

Documents with assembly mates, a grounded instance, a supported construction
reference, a `native_solid`, or a sheet-metal feature that stores a `base`
panel use version 7. Fixed instance flags, mate definitions, and the sheet
authoring graph are saved with the expressions. Solved transforms and display
meshes are derived on load.

## Version 8: extended surfaces and sampled meshes

Documents containing `sweep`, `extrude_surface`, `revolve_surface`,
`offset_surface`, `sew`, or any `mesh_body` / `mesh_weld` / `mesh_smooth` /
`mesh_decimate` / `mesh_offset` / `mesh_fill` feature use version 8. The five
exact surfaces store a `native` object, as earlier native features do.
`mesh_body` stores the authored `mesh` object with `positions` and `indices`;
the evaluated mesh and its report are not history edits. A mesh operation does
not store a B-rep. A document that does not use these features keeps its
previous version. A reader that sees one of these features below version 8
rejects the file.

## Version 9: draft, patterns, mirror, edge selection, and loft tips

Save chooses the version. A document stays at its previous version until it
uses one of these authored choices: a `native_solid` operation `draft`,
`linear_pattern`, `circular_pattern`, or `mirror`; a fillet or chamfer
`native.selection`; a second chamfer distance; loft `p.smooth` or
`p.compatible` set to `1`; a loft `p.continuity` other than `1`; or a loft
section whose sketch is a single point. Mirror `native.plane` may be `XY`,
`XZ`, `YZ`, or a named datum. Mirror `native.face` names one planar face of
the seed solid. Viewport selection writes those same names; it does not add
another document version. Older readers reject that file instead of ignoring
the new definition. Versions 1 through 8 still load and save at their own
version. The product version stays 0.9.0.
