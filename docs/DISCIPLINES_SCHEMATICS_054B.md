# Families, electrical schematics and flexible motion — 054B

Scope: **A.02, B.04, B.05, C.04, D.02**. These broader milestones remain
**Parcial**. This iteration adds local authoring and display contracts, with
verification recorded in [054B](iterations/054B.md).

## Families and Eye mode

Definitions and assembly occurrences/groups accept `classification` metadata:
`families` and free-form `tags`. Published families are `electrical`,
`electronics`, `mechanical`, `bodywork`, `sheet_metal`, and `plastics`.
Multiple families are permitted. The nearest occurrence/group with nonempty
families overrides its ancestors; otherwise the definition supplies them.
Tags remain attached to their declared object.

In Studio, **Inspect → Families** edits the selected definition, occurrence or
group. **Inspect → Eye mode** shows all, unclassified, or the union of selected
families. Filtering affects display and picking; it changes no geometry, mate,
BOM quantity or document revision. A newly opened document starts with all
families visible. Existing visibility and loading rules still apply.

Agents use `doc_apply` with the observed `expected_revision`:

```json
{"commands":[{"op":"classification.set","feature":12,
  "families":["bodywork","sheet_metal"],"tags":["housing","removable"]}],
 "expected_revision":7}
```

Use `node` instead of `feature` for an occurrence or group. Omitted fields keep
their previous values; empty arrays clear them. `classification_info{feature}`
or `{node}` inspects one object; no selector lists declarations. There are at
most six families and sixteen unique tags of at most 64 bytes each. These are
design classifications, not electrical ratings, material specifications or
manufacturing evidence. Reusable `.arkprt`/`.arktop` recipes retain declarations.

## Editable electrical sheets

**Inspect → Schematic** opens the native Schematic Designer. New creates a sheet;
Symbol adds a block, connector, resistor, switch, ground or power symbol. Symbol
properties edit the reference, position, rotation, procurement metadata and
optional CAD source. Pins edits stable pin IDs and their local coordinates.
Wire connects two explicitly selected pins and asks for a net name. Properties
edits the selected wire's net and bend points. Dragging a symbol previews locally
and publishes one revision on release. Undo/redo uses the document history.

The side panel is the sheet's BOM. Each included symbol contributes one component
reference, with explicit metadata taking priority over linked CAD part metadata.
**Include in BOM: No** excludes ground/net annotations without deleting their
connections. A symbol may bind `feature:ID` or `node:ID` with an explicit pattern
copy. Selecting it selects that source in 3D. Removing a referenced CAD source
rejects until its schematic binding is updated or removed.

The source contract is `schematic.set{sheet}` or `schematic.remove{name}` through
revision-checked `doc_apply`/`doc_preview`. A sheet replacement is atomic:

```json
{"name":"Control","width":297,"height":210,
 "symbols":[
  {"id":"supply","ref":"X1","kind":"connector","x":40,"y":50,
   "part_number":"DECLARED-EXAMPLE","pins":[{"id":"1","x":10,"y":0}]},
  {"id":"load","ref":"R1","kind":"resistor","x":100,"y":50,
   "pins":[{"id":"1","x":-10,"y":0},{"id":"2","x":10,"y":0}]}],
 "wires":[{"id":"w1","net":"SUPPLY",
  "from":{"symbol":"supply","pin":"1"},
  "to":{"symbol":"load","pin":"1"},"points":[]}]}
```

Sheet fields: `name`, optional `title`, `width` and `height`, `symbols`, `wires`.
Symbol fields: `id`, `ref`, `kind`, `x`, `y`, `pins`, optional `label`,
`rotation` (0/90/180/270), `width`, `height`, `feature` or `node`, `copy`,
`part_number`, `manufacturer`, `description`, `bom` (default true).
Pin fields: `id`, optional `label`, `x`, `y`. Wire fields: `id`, `net`,
`from`/`to` (`symbol`, `pin`), optional `points` as sheet-mm coordinate pairs.

`schematic_inspect{name}` returns the editable sheet, nets, unconnected pins and
its BOM; no name lists sheets. Crossings do not create connections. Equal net
names join their declared pin endpoints. One pin cannot have two different net
names. Component references and symbol IDs are unique within a sheet; one CAD
occurrence copy cannot represent two components on that sheet. Source limits:
32 sheets, 512 symbols and 2048 wires per sheet, 64 pins per symbol, 64 bend
points per wire. Sheet dimensions are 50–2000 mm; anchors and pins stay inside.

The same drawing primitives feed the editor and `schematic_export`:
`{name,format:"dxf"|"svg"|"bom_csv",path,expected_revision}`. Exports publish to
a new path; existing files and stale-revision exports reject. DXF contains
millimetre LINE/TEXT entities on SYMBOLS/WIRES/PINS/LABELS layers. The `.ark`
retains electrical connectivity; DXF is graphical exchange, not a netlist.
The editor does not simulate circuits, select conductor sizes, infer voltage
ratings, or qualify compliance with an electrical drafting/safety standard.

Classifications and sheets require conditional authoring format **52**. The
binary container is unchanged. Ordinary documents without these fields keep
their existing semantic version. Schematics are project-level context and are
excluded from private part checkout and reusable component source closures.

## Cables in baked motion

A `cable_route` with start/end/waypoint or obstacle bindings to moving occurrence
IDs is prepared at the recorded assembly poses before Studio playback. For each
distinct pose, the existing native cable producer constructs and validates the
sweep. Invalid routes report the cable and frame; they are never replaced by a
frozen cable. Preparation runs in the background, displays progress and supports
Stop/cancellation. Repeated requests share geometry; the derived display cache
is compressed losslessly when the platform codec is available.

Only display meshes change during playback. Stop restores the original mesh and
picking data. Document recipes, evaluated B-reps, history and revisions remain
unchanged. The cache is private to this process and invalidates on source/motion
replacement. Bound-cable geometry participates in the motion source digest;
families, schematic sheets and appearance do not. A stale motion with flexible
cables requires an explicit **Bake** before playback. Legacy rigid-only stale
playback retains its existing inspection behavior and STALE badge.

Capacity is 32 flexible cables, 32768 cable-frame entries and 512 MiB of retained
display data. Capacity errors ask for fewer frames; no geometry is discarded.
Cold preparation cost scales with distinct poses. Native validity at sampled
poses does not prove constant cable length, strain, fatigue, dynamics, supplier
qualification or continuous clearance between frames. Rigid motion clearance
rejects any requested scope containing a deforming cable; explicit rigid-only
pairs remain queryable. Animated image/export consumers outside Studio's baked
playback have not acquired this display cache contract.
