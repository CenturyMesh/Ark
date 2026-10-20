# Current source semantics — format 43

031B conditionally selects source43 for the retained solid recipes, advanced
fillet/chamfer/split semantics, constrained fill/blend, surface extend/untrim/
mutual trim, sweep orientation/law recipes, new indexed mesh edit operations,
and occurrence-bound cable endpoints/obstacles. Numeric node and producer IDs
retain dependency custody. The prior source and all authored expressions remain
editable; evaluated B-reps, triangle reports, pose samples and reference frames
are derived data. A relabelled older source rejects rather than ignoring these
recipes, including through iteration history. Physical container version 2 adds
optional evaluated native geometry independently of source43. Older documents
retain their actual required earlier source format; container version 1 remains
readable.

030D's source42 remains the retained format for emission, authored mesh nets
and exact Wood/Vent recipes without newer authoring. See [031B](iterations/031B.md)
and the individual [solid](SOLIDS_031B.md), [surface](SURFACES_031B.md),
[mesh](MESHES_031B.md) and [assembly/cable](ASSEMBLY_SWEEP_031B.md) contracts.

## Conditional source extensions — formats 45 and 46

Smooth/ribbon cable recipes and endpoint electrical metadata select format 45.
A stored bisector plane (`support.kind` `bisector`) selects format 46, including
documents that also contain those cables. Format 44 remains the conditional
version for assembly pattern carry frames. Implicit Origin, XY, XZ and YZ are
not stored and do not select a format. Legacy authored datums named Origin
retain their source and reference identity when reopened. A file at version 45
or 46 without the corresponding extension still loads. Bisectors stored below
46 and cable extensions stored below 45 reject.

## Conditional source extension — format 47

044B selects format 47 for a `curve_pattern` with chained/whole-wire paths,
pitch spacing, orientation, support input or the compact reference scheme
([curve patterns](CURVE_PATTERNS_044B.md)), and for procedural appearance
`pattern*` fields, `image/gif` or `video/mp4` assets and non-default stamp
playback ([media](MEDIA_TEXTURES_044B.md)). Documents without them keep their
earlier version; those fields below 47 reject. Version 48 and later reject as a
future format.

## Previous source extension — format 41

029B conditionally selects source 41 for explicit `surface_edit` orientation
repair and `ark.design-container.v2` workspaces. Repair keeps the single input,
orientation mode and optional planar normal as editable source. Container v2
retains explicit configuration ownership, feature-owned external pins and a
workspace-local load selection; merge preserves the parent's selector. Legacy
v1 workspaces remain readable. Downgraded files reject, including in retained
iteration history. Ordinary documents keep their earlier required format;
the binary container schema stays at version 1.

See [surface repair](SURFACE_REPAIR_029B.md) and
[assembly scaling](ASSEMBLY_SCALING_029B.md).

## Previous source extension — format 40

029B conditionally selects source 40 for an inclusive `history_tip`, named
`assembly_configurations`, `assembly_load_selection` or
`assembly_external_references`. The rollback suffix remains authored; selection
controls evaluation rather than deleting definitions or occurrences. External
pins retain the imported producer hash, absolute local source path and explicit
revision label; changed/missing source bytes reject protected use and publication.
Older runtimes reject these fields instead of silently ignoring them. The
iteration journal accepts source 40; the binary container schema remains version 1.
Ordinary documents continue to select their actual required earlier format.
See [029B](iterations/029B.md), [history/configuration authoring](CAD_AUTHORING_FOUNDATION.md)
and [configuration controls](PRODUCT_CONFIGURATIONS_UI.md).

## Previous source extension — format 39

024B conditionally uses 39 for embedded image assets, projected Stamps,
face appearances and exact split-line regions. Their original structures and
source digests remain intact when combined with source 40 authoring. Resolved
geometry and display projection stay derived. See [024B](iterations/024B.md).

## Previous source extension — format 38

023B selects format 38 only for exact `imported_cad` source packages,
`holes_orchestrator` recipes or an isolated `design_container_checkout` manifest.
Embedded original exchange bytes and transferred B-rep hashes preserve import
custody. Hole parameters, named locations and semantic callouts remain authored
source. Container baselines, ownership and parent digests persist for guarded
merge. Loading these recipes under an earlier format explicitly rejects; the
iteration journal accepts 38. The binary container schema stays at version 1.
See [023B](iterations/023B.md) and its linked contracts.

## Previous source extension — format 37

022B conditionally uses 37 for exact polyhedral vertex pulls, linked specialized
component patterns, extended sheet programs and drawing source/template recipes.
Release identity alone does not advance a document format. See
[022B](iterations/022B.md).

## Previous source extension — format 36

021B keeps application identity separate from the `.ark` source format. A file
selects version 36 only when it contains authored associative technical drawings
or a native extrusion with reverse/symmetric, exact source-face or `up_to_face`
control. Drawing source IDs, exact B-rep/topology digests, observed revisions,
views, dimensions and caller-declared tolerances persist; derived curves and
display previews do not. Extended extrusion inputs retain their authored
source/limit references and continue to recompute from exact geometry. A file
labelled below 36 with either source contract rejects on load. Durable iteration
history accepts version 36. See [021B](iterations/021B.md), [drawings](DRAWINGS.md)
and [viewport extrusion](VIEWPORT_DIMENSIONS.md).

## Previous source extension — format 35

019B accepts legacy formats 1–34 and writes format 35 for native Skeleton source,
indexed `mesh_edit` recipes, indexed vector-coordinate expressions, helix sources,
extended loft controls, connected surface-sewing policy and authored references
to current-B-rep fallback face/edge handles. Skeleton source,
overrides and ownership persist; evaluated geometry and static collision reports
remain derived. Version gates cover nested sketch/native sources, tube programs,
datums, assemblies and mate expressions. History also supports version 35.
Older runtimes reject unsupported semantics. See [019B](iterations/019B.md).

Derived native picking inventories alone do not advance the source format.
Fallback handles include the complete B-rep SHA-256 and a verified topology
index; they are scoped to that exact B-rep and are not persistent associative
names. A changed B-rep makes a retained handle obsolete. See
[native selection coverage](PARAMETRIC_GEOMETRY_019B.md#local-verification-scope).

## Previous source extension — format 34

018B accepts legacy formats 1–33 and adds format 34 only when a document uses
typed sketch recipes, new associative construction recipes, spatial constraints
or anchors, interpolating spatial splines, surface splines, analytic equations or
varying sweep sections. These source fields persist; resolved geometry remains
derived. Older runtimes reject the unsupported format. Application iteration and
document format are independent. See [018B](iterations/018B.md) for that scope.

# ARK file formats, document versions 1 through 41

## `.ark`

An owned binary container carries editable design intent and persistent iterations.
Container version 2 can additionally carry evaluated native geometry for reopening
on a machine without the original local cache. These are native OCCT B-reps and
associated result data, not STEP exchange files. The editable feature recipes and
iteration history remain in their existing authored representation.
See [binary storage and recovery](DOCUMENT_CONTAINER.md) and [iteration history](ITERATION_HISTORY.md).
The loader detects the signature and still accepts legacy JSON `.ark` files.
Explicit `.arkjson` and `.json` exports retain the versioned JSON representation, for example:

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

Serialization sorts object keys for deterministic diffs. Binary saves update changed chunks in a SQLite transaction; initial saves and legacy JSON migrations use a staged sibling replacement. Upgrading a version-1 binary container to version 2 changes the schema, authored chunks and optional geometry in one transaction; failure rolls all of them back. A concurrent write lock causes a save failure; independently opened sessions do not merge edits or detect intervening completed saves. Loaded content is parsed into scratch state; the agent API commits only after successful geometry evaluation or supported saved-geometry validation.

The version-2 `ark_derived` table stores independently hashed payloads keyed by
the exact normalized native request and producer executable/dependency identity.
Normal dependency traversal still constructs the current requests; only exact
matches are candidates for reuse. A matching portable entry must pass native
B-rep validation and regeneration of current geometric measurements, references
and display mesh. Operation-specific diagnostics retained from a previous
evaluation are identified as saved evidence, not proof of a new recipe replay.
Unsupported operations, changed inputs or producers, and rejected entries use
normal evaluation. First opening on another machine therefore does not promise
the timing of an already validated local cache hit.

Authored content retains its **4 MiB logical limit** and **16 MiB typed-value and
authored payload limits**. Optional derived data has a separate **64 MiB per-entry
limit**, **128 MiB total limit** and at most **8192 entries**; the physical file
limit is **256 MiB**. Derived bytes do not enter the authoring hash or iteration
history. A malformed optional payload discards the derived section with an
explicit diagnostic; authoring checksum, schema and structural database failures
remain hard load failures. Missing geometry can be rebuilt from valid authoring.

Since 045A a save never fails because the optional section is full. Entries
that would exceed the per-entry, total or entry-count limit are omitted in
document order and evaluated from authoring on the next open. The `doc_save`
receipt reports `saved_geometry` (`status` `saved` or `partial`,
`entries_saved`, `bytes_saved`, `entries_omitted`, `bytes_omitted`,
`capacity_bytes`), and Studio states a partial save in its status bar. A failed
save now returns its reason instead of only "Could not save document".

New authoring-only binary saves remain version 1. Adding saved geometry upgrades
to version 2. Saving an existing version-2 file without derived entries removes
stale entries and keeps version 2. Physical format downgrade is unsupported;
older runtimes reject the newer container. Explicit `.arkjson`/`.json` exports
remain authoring/history exports and do not carry this optional evaluated cache.

Camera, UI layout and session revision are not persisted. Display meshes may be
present inside optional saved native results, but portable restoration regenerates
them from the B-rep. Iteration nodes, branches and undo/redo targets are persisted
in document version 27. The physical container version is independent of the
document and session versions.

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

The current reader accepts versions 1 through 36. Older ARK executables reject a newer
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

## Version 10: instance patterns and multi-solid exact booleans

Save chooses the version. A document becomes version 10 only when an assembly
node has an active `pattern`, a `native_solid` stores `native.instances`, or
a `native_solid` `union`, `cut` or `common` (including the `boolean_*` and
`intersect` aliases) has more than two feature inputs. The pattern object
stores `type`, `count`, and either `dx`/`dy`/`dz` or `step`/`axis`. Copies are
derived on load and are not stored one by one. Feature and mate name
references are resolved before save, so the file still stores numeric ids.
A two-input boolean stays at version 7. Finishing features without a pattern
or a multi-solid boolean stay at version 9. A file below version 10 that
contains this data is rejected and the error names version 10. Versions 1
through 9 still load and save at their own version. The product version stays
0.9.0.

## Version 11: solid shells and loft guide curves

Save chooses the version. A document becomes version 11 only when a `loft`
stores a non-empty `native.guides` array, or a `native_solid` operation is
`shell`. `shell` keeps signed `p.thickness` and optional `native.faces` naming
the faces that open. Guide entries are sketch names. An empty `guides` array
does not select version 11. Merged face aliases, a round-tube exact B-rep,
clearance, measure, name binding and snapshot copy are derived or request-only
and do not change the document version. A file below version 11 that contains
a shell or loft guides is rejected and the error names version 11. Versions 1
through 10 still load and save at their own version. The product version stays
0.9.0.

## Version 12: points, sketch arcs and wires, and curve features

Save chooses the version. A document becomes version 12 only when a datum
kind is `point`, a datum support kind is `on_curve` or `on_face`, a sketch
entity is an `arc` or a `wire`, a sketch constraint is `tangent`, a feature
type is `curve`, `intersection_curve` or `bridge`, or a loft section, loft
guide or sweep section/path names a point datum or a curve feature. Sketch
names win over datum names, which win over feature names. An empty version
12 file still loads. A file below version 12 that contains this data is
rejected and the error says that points, curve segments, or curve features
require document version 12. Versions 1 through 11 still load and save at
their own version.

A sketch arc stores `[cx,cy,r,a0_deg,a1_deg]`. A wire stores segment kind and
sweep plus a flat degree-of-freedom list. A curve stores `native.start` and
`native.segments` (`line`, `arc` or `nurbs`). The wire B-rep is the authority.
The 0.3 mm display ribbon is evidence. `intersection_curve` stores one solid
input and a datum plane name and must produce one wire. `bridge` stores two
semantic edges. Continuity 1 is a cubic whose end derivative is the unit
edge tangent times the chord length. Continuity 2 is a quintic with those
tangents and zero end curvature; it does not copy sampled
curvature from the source edge and it is not a surface blend. Merged face
aliases, a round-tube exact B-rep, clearance, measure, name binding and
snapshot copy still do not change the document version. The product version
stays 0.9.0.

## Version 13: plane canvases, suppressed features, and driven sketch entities

Save chooses the version. A document becomes version 13 only when it has a
plane canvas, a feature with `suppressed` true, or a sketch entity of kind
`project` or `offset`. An empty version 13 file still loads. A file below
version 13 that contains any of those is rejected and the error says that
plane canvases, suppressed features, or projected and offset sketch entities
require document version 13. Versions 1 through 12 still load and save at
their own version. A photo grid on `mesh_body` stays version 8. The product
version stays 0.9.0.

A canvas stores `name`, `plane`, `path`, `width`, `height` and `opacity`.
The kernel keeps the path and the rectangle in millimetres. It does not
decode pixels. Studio draws the image, and a missing file does not fail
recompute. `suppressed: true` keeps the feature in the history and leaves
its body out of recomputation. A project stores a feature id and a semantic
edge or planar face. An offset stores an earlier entity name and a signed
distance. Positive distance is to the left of the directed curve.

## Version 14: sketch fillets, sketch chamfers, and mates across groups

Save chooses the version. A document becomes version 14 only when a sketch
stores a fillet or chamfer corner, or a mate joins part instances whose
immediate parents differ. An empty version 14 file still loads. A file below
version 14 that contains either of those is rejected and the error says that
sketch fillets, sketch chamfers, or mates across assembly groups require
document version 14. `p.density` and a rectangular-tube exact B-rep do not
change the version. Versions 1 through 13 still load and save at their own
version. The product version stays 0.9.0.

A corner record stores `entity`, `corner`, `kind` (`fillet` or `chamfer`) and
`size`. `size` is the radius or the chamfer distance, kept as an expression.
The sharp wire stays the authority. At most 64 corners are stored, and the
same entity and corner index cannot appear twice. A rectangular tube exact
solid is a hollow sweep of a rectangular section. The centerline starts along
+X, width lies along the section normal, and height lies along the binormal.
The display mesh remains the tessellation of that centerline.

## Version 15: materials and plane splitting

Materials, feature appearance overrides, `native_solid` `split_plane`, and a
mirror using `p.copy_only` require version 15. Older documents retain their
previous version when none of these are present.

The document `materials` array stores each material's name, nominal density
in g/cm3 and appearance. Features optionally store `material` and `appearance`.
Appearance includes sRGB `base_color`, `metallic`, `roughness`, `opacity`,
`texture_path`, and `texture_scale`, plus the format-47 procedural `pattern`,
`pattern_scale_mm`, `pattern_angle_deg`, `pattern_contrast` and `pattern_color`. Image pixels remain external files; moving
a document does not copy its textures. These display properties do not change
the B-rep. An explicit feature `p.density` overrides material density.

Plane splits store `native.keep` (`positive`, `negative` or `both`) and their
plane selector alongside the input feature. Copy-only mirrors store
`p.copy_only="1"`. Both operations retain editable expressions and exact
native geometry. See [surfacing](SURFACING.md) and
[materials and rendering](RENDER_MATERIALS.md).

## Version 16: engineering modules

Features `construction_frame`, `pcb` and `cable_route` preserve their authored
program in `native`. Numeric fields can retain expression strings. Frame
programs store named profiles, nodes, members and supported joints; PCB programs
store their layout/net data and design rules; cable programs store endpoints,
waypoints, bend parameters and declared obstacles. Evaluated B-reps, display
meshes and reports are recomputed rather than serialized. A lower-version file
containing these feature types is rejected.

Plane supports can be authored with a unique producer feature name through
`doc_apply`; saved supports store its numeric ID. The UI's module tab, loading
overlay and camera controls do not change file version.

## Version 17: drawing usage receipts and sheet openings

The optional `usage` object stores receipt IDs, sources, provenance, model,
UTC timestamps and provider-reported counts. Its serial changes independently
of geometry revision. Geometry undo/redo preserves the current ledger; save/load
preserves receipts without double counting. External token coverage is always
explicitly unknown. An empty ledger is omitted from the file.

Authored sheet `slot` and `window` openings also select version 17. A downgraded
file containing them or a usage ledger is rejected. Circle/polygon patterns
expand to normal named opening records and do not require a newer version alone.

## Version 18: suspension hardpoints

The `suspension` feature preserves its authored hardpoint, wheel, spring and
travel-limit expressions in `native`. Its nominal mesh and solved poses are
recomputed. Interactive motion and Spotlight visibility are display-only and do
not alter the saved geometry. Older readers reject version 18; current readers
retain compatibility with versions 1 through 17. See [suspension](SUSPENSION.md).
