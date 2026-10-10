# Mechanical intent and feature discovery

Call `agent_onboard{topic:"manufacturing"}` before authoring mechanical parts.
Start with functional interfaces, the design envelope, datums and primary
dimensions. Specify material, stock or thickness, bend radius and the fabrication
route before cosmetic detail. Define mounting holes from functional datums and
inspect assembly clearances after placement.

| Intended component | Authoring route | Evidence to inspect |
| --- | --- | --- |
| Folded skin, tray or bracket | `sheet.create`, flanges, panel openings, hems and supported embosses | `sheet_inspect`, formed B-rep, flat qualification limits |
| Machined part | `native_solid` primitive/sketch extrusion followed by exact hole, Boolean and edge operations | `native_inspect`, dimensions and semantic references |
| Welded tube frame | `construction_frame` profiles, nodes, members and supported cope joints | `frame_inspect` cutlist, lengths and joint evidence |
| Purchased device | Explicit supplier envelope and mounting interfaces | Supplier documentation and installation clearance |

Declare intent with `feature.add` or `feature.edit`:

```json
{"op":"feature.edit","id":"Installed tray","manufacturing":{"route":"sheet_metal"}}
```

Routes are `sheet_metal`, `machined`, `frame`, `purchased` and `unspecified`.
`manufacturing:null` clears the declaration. This metadata is saved in document
version 21; older documents remain readable with undeclared intent. A sheet or
frame declaration must match the actual authoring lineage. A machined declaration
requires exact solid authoring rather than a legacy display-only body. A mismatch
rejects the whole `doc_apply` batch and preserves the document and revision.
Use `expected_revision` for edits based on a prior inspection.

`doc_info{detail:"design"}` exposes each feature's `type`, `operation`, `inputs`,
`dependents`, referenced `sketches`, assembly `instances` and `role`. Its
`manufacturing` block distinguishes the declared route from the geometry route.
Sketch rows contain entity names, kinds and construction status, projected source
references, point references and consumer feature IDs. Use `sketch_inspect` for
resolved coordinates or `reference_inspect` for exact semantic topology.

A `native_solid` with operation `transform` is a rigid placement of its input.
If that input originates from a sheet, `manufacturing.sheet_origin` identifies
the authoring sheet and `editable_sheet` distinguishes designed sheets from
read-only source imports. `sheet_inspect` and `export_flat_dxf` accept a rigid
sheet placement and operate in the originating sheet's local coordinates. Pass
the origin ID to sheet edit commands, flat STEP/IGES export and bend preview.
Generic Boolean/hole edits outside the sheet program terminate editable flat
lineage. Use panel-owned `sheet.opening` commands for supported openings instead.
Names, colours and small thickness alone do not establish sheet provenance.

The user-owned `examples/kioks.ark` was inspected without modification: its seven
sheet blanks have IDs 1, 3, 6, 8, 10, 19 and 47. Their eight installed placements
have IDs 2, 4, 5, 7, 9, 11, 20 and 48. The native transforms retain those sheet
origins. Feature 12 is a construction frame. Other generic exact solids do not
prove a manufacturing route or supplier identity.

Hems and stamps remain in the sheet program. Supported circular embosses carry
exact formed geometry and a nominal flat reference, with
`flat_pattern_qualified:false`. Flat STEP, IGES and DXF export reject that
reference; intermediate stamp deformation is not a bend simulation. Bend
allowances/K-factors are declared geometric models, not calibrated production
deductions. Check stock, tool access, clearances, bend tooling, material strain,
fasteners and weld specifications separately. `manufacturing_qualified` remains
false: geometric validation does not certify the fabrication process.
