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
| Injection molded plastic | Finished-part exact `native_solid`, including deliberate shells, ribs, bosses and supported draft operations | `manufacturing_inspect` mold intent, exact body and unknown mold checks |
| Additive plastic | Exact `native_solid` with an explicit FDM process plan | `manufacturing_inspect` layer/orientation/slicer intent and filament basis |
| Welded tube frame | `construction_frame` profiles, nodes, members and supported cope joints | `frame_inspect` cutlist, lengths and joint evidence |
| Purchased device | Explicit supplier envelope and mounting interfaces | Supplier documentation and installation clearance |

Declare intent with `feature.add` or `feature.edit`:

```json
{"op":"feature.edit","id":"Installed tray","manufacturing":{"route":"sheet_metal"}}
```

Routes are `sheet_metal`, `machined`, `frame`, `purchased`, `unspecified`,
`injection_molding` and `additive`. Material names do not choose a process:
a plastic part may be machined, molded or printed with different constraints.
`manufacturing:null` clears both the route and process. A manufacturing object
replaces the complete plan; include all settings to retain them when editing.
New plastic routes and nonempty process settings save as document version 23.
Earlier routes without process settings retain version 21; older documents remain
readable with undeclared intent. A sheet or frame declaration must match the actual
authoring lineage. Machined, injection and additive declarations require exact
solid authoring rather than a legacy display-only body. A mismatch
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

## Process inspection

`manufacturing_inspect{id:"Part"}` is read-only. Omit `id` to inspect declared and
output feature definitions, including placed definitions. It reports the current
revision, `components`, `geometry_mutated:false` and `manufacturing_qualified:false`.
Each component carries its declared route, geometry lineage, process settings,
compact exact B-rep evidence, requirements, blockers, unknowns and design policies.
No geometry is recomputed or promoted, and no display mesh is measured for a
manufacturing decision. A native authoring type without an evaluated B-rep blocks
the exact-solid requirement. A component's bounds and volume come from its current
OCCT report, not from guessed dimensions.

Requirements use `verified` for the available exact B-rep check, `declared` for
caller process inputs, `unknown` for missing inputs or unimplemented checks, and
`blocked` for an explicit conflict. `process_parameters_complete` only means the
required process declarations are present without blockers. Unknown strength,
tooling and process checks still keep `ready_for_process_review:false`. That flag
does not establish production qualification even if future checks clear it.
Read the individual requirements before delivery. This tool also works inside
`query_batch` and required `doc_apply` postconditions.

All process dimensions are explicit finite numeric millimetres. Directions and
origins use feature-local coordinates. Directions contain three finite values
and must be nonzero; sizes contain three positive values. Strings contain 1..1024
characters, counts are integers 1..1000, and percentages are 0..100. Unknown fields,
settings belonging to another route, malformed vectors and unsupported methods
reject atomically, including on document load. Common settings are
`material_grade`, `tolerance_mm`, `load_case`, and optional `finish`.

## Injection molded plastic

```json
{
  "op":"feature.edit", "id":"Housing",
  "manufacturing":{
    "route":"injection_molding",
    "process":{
      "material_grade":"supplier-selected resin grade",
      "tolerance_mm":0.1, "load_case":"declared enclosure loads and temperature",
      "nominal_wall_mm":2, "min_draft_deg":1, "pull_direction":[0,0,1],
      "parting_line":"tooling split to be reviewed",
      "undercut_strategy":"none", "gate_plan":"supplier gate review",
      "ejection_plan":"supplier ejection review", "shrinkage_percent":0.5
    }
  }
}
```

The example numbers are caller inputs, not recommended defaults or validated
resin data. `min_draft_deg` accepts 0..less than 90 degrees.
`undercut_strategy` accepts `none`, `side_action`, `insert` or `redesign`.
Keep walls consistent, core out excessive mass, and define ribs, bosses and
rounded transitions for the chosen resin. Review every relevant surface against
the pull direction and parting line. Specify gate and ejection locations with
the supplier; flow, cooling, shrinkage and finish affect the result. These are
the design factors described in [Protolabs' molding design guidance](https://www.protolabs.com/resources/design-tips/injection-molding-basics/)
and [draft guidance](https://www.protolabs.com/resources/design-tips/improving-part-moldability-with-draft/).

An authored `draft` operation or a target angle does not prove all faces release.
Wall distribution, trapped geometry, mold flow and tooling remain explicit
unknown requirements. Keep finished-part CAD as dimensional authority; mold
compensation requires a separate explicit tooling design and supplier data.
Printer nozzle, infill and filament settings are rejected in this route.

## Additive plastic and filament use

The current bounded additive method is FDM. `method:"sla"` and other methods
reject explicitly because their process rules are different.

```json
{
  "op":"feature.edit", "id":"Printed housing",
  "manufacturing":{
    "route":"additive",
    "process":{
      "method":"fdm", "material_grade":"supplier filament grade",
      "tolerance_mm":0.2, "load_case":"declared enclosure loads and temperature",
      "build_direction":[0,0,1], "nozzle_diameter_mm":0.4,
      "layer_height_mm":0.2, "line_width_mm":0.45, "perimeters":3,
      "top_layers":5, "bottom_layers":5, "infill_percent":15,
      "support_strategy":"none", "filament_diameter_mm":1.75,
      "slicer_profile":"printer/material profile version",
      "build_volume_mm":[250,210,220]
    }
  }
}
```

These values also require a deliberate user/process choice. `support_strategy`
accepts `none`, `build_plate`, `everywhere`, `soluble` or `custom`. The planning
domain blocks layer heights at or above the nozzle diameter; the printer's actual
profile supplies its supported limit. Shell-band and top/bottom-skin arithmetic
are labeled caller-settings arithmetic, not measured CAD wall thickness.

Orient the part against loads and layer adhesion, functional detail resolution,
overhangs, support access and machine fit. Preserve mounting interfaces, walls and
load paths while comparing actual slicer shell, infill, support and purge use.
Perimeters and infill affect strength and material use differently, as described
in [Prusa's infill documentation](https://help.prusa3d.com/article/infill_42).
The inspection does not invent a strength result or a support-free print decision.

With a checked exact solid and declared filament diameter, `filament_budget`
includes `solid_equivalent_length_mm = volume_mm3 / (pi * diameter_mm^2 / 4)`.
This is a fully solid equivalent. It excludes slicer infill/shell paths, support,
brim/raft, purge, extrusion calibration and waste. The tool retains
`actual_consumption_checked:false` and `actual_filament_length_mm:null`; actual
consumption and claimed saving require slicer evidence for the chosen settings.
Injection gate/draft/shrinkage fields are rejected in this route.

## Machining process plan

Machined process settings add `stock_size_mm`, `stock_origin_mm`, `tool_axis`,
`cutter_diameter_mm`, `minimum_internal_radius_mm`, `workholding_plan` and
`setup_plan` to the common material/tolerance/load inputs. Stock is an explicit
axis-aligned box in feature-local coordinates. Available exact B-rep bounds can
prove that it encloses the part; they cannot prove clamping or cutting access.
A declared internal radius smaller than half the declared cutter diameter adds a
blocker, labeled as a comparison of caller targets rather than measured geometry.
Tool and holder accessibility, setups, fixture collisions, deflection, finish and
attainable tolerance remain unknown until independently checked.
