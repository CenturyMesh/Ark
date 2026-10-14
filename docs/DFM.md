# Process-specific DFM screening (C.03)

Call `agent_onboard{topic:"manufacturing"}` and then
`manufacturing_inspect{id:"Part"}`. Each component returns a `dfm` object with
`schema_version:1`, `ruleset_id:"ark.dfm"`, `ruleset_version:1`, `rules` and
`recommendations`. The existing tool remains available to standalone MCP,
Studio agents, `query_batch` and inspection postconditions.

The inspector uses the current evaluated exact B-rep and producer reports.
It never measures a display mesh, changes geometry, runs CAM/a slicer, chooses a
process from appearance, or qualifies manufacture. Every rule carries its own
version, process, status, evidence, authority, assumptions and guidance.

| Status | Meaning |
| --- | --- |
| `compatible` | The named bounded comparison satisfies the supplied inputs. |
| `incompatible` | The comparison conflicts with supplied inputs; its rule ID also appears in component `blockers`. |
| `unknown` | Required input, exact evidence or implemented analysis is absent. |
| `not_applicable` | The explicit route/method or evaluated operation census excludes this comparison. |

Editable candidate review is available through [DFM proposals](DFM_PROPOSALS.md):
`manufacturing_propose` compares an explicit revision-bound edit against the
same process inputs and requires explicit acceptance before atomic commit.

Recommendations have `rule_id`, `rule_version`, `kind`, `severity`, `guidance`
and `automatic_edit:false`. Kinds are `request_input`,
`change_process_or_design` and `obtain_process_evidence`. They propose review
actions; applying a design change still requires an explicit revision-checked
`doc_apply`. Compatible comparisons can coexist with unresolved strength,
tooling or process checks. `manufacturing_qualified` always remains false.

## Ruleset 1

| Rule | Process | Bounded check and authority | Remaining evidence |
| --- | --- | --- | --- |
| `DFM.FDM.01` | FDM | Declared layer height is smaller than declared nozzle diameter; caller arithmetic. | Actual printer/material/profile capability. |
| `DFM.FDM.02` | FDM | Exact B-rep envelope against declared build volume for feature-local +Z/-Z. Machine X/Y align with local X/Y; no in-plane rotation; translation is free. | Arbitrary orientation, bed margins, supports, brim/raft, purge and slicer fit. |
| `DFM.FDM.03` | FDM | Explicit unresolved process evidence. | Overhangs/bridging, support removal, layer strength and declared load case. |
| `DFM.CNC.01` | CNC | Exact B-rep bounds inside declared feature-local axis-aligned stock size/origin. | Clamps, allowances, machine/tool/holder access and setups. |
| `DFM.CNC.02` | CNC | Declared minimum internal radius is at least declared cutter radius. | Actual corner geometry, toolpath and holder access; the declared radius is not a measured B-rep radius. |
| `DFM.CNC.03` | CNC | Explicit unresolved process evidence. | Workholding, setup collision, deflection and attainable tolerance. |
| `DFM.LASER.01` | Laser | Evaluated sheet thickness against a caller-supplied material/machine maximum, only for `cutting_method:laser`. | Kerf, small holes/webs, heat, lead-ins and cut quality. |
| `DFM.LASER.02` | Laser | Producer-supported geometric developed blank; stamped nominal flats are incompatible. | Production calibration and cutting capability. |
| `DFM.BEND.01` | Bend | Every originating sheet model's evaluated bend radius against caller minimum; includes reported hems/jogs. No bends is `not_applicable`. Missing bend inventory is `unknown`. | Material/tooling capability, calibration, springback, sequence and access. |
| `DFM.BEND.02` | Bend | Explicit unresolved process evidence. | Material-specific deductions, springback, tooling and sequence. |
| `DFM.WELD.01` | Weld | Every evaluated declared triangular bead's theoretical throat against caller minimum; producer checks attachment to both exact members. No beads is missing evidence. | Continuous fusion, penetration, strength, quality and access. |
| `DFM.WELD.02` | Weld | Explicit unresolved process evidence. | Welding procedure, material compatibility, joint strength, fixtures/distortion and inspection. |
| `DFM.MOLD.01` | Injection molding | Explicit unresolved process evidence. | Walls, draft, undercuts, flow/cooling and tooling for actual resin/pull direction. |

Envelope comparisons retain the existing stock inspector's `1e-7 mm` numerical
comparison epsilon, recorded in evidence. It is not a manufacturing tolerance or
permission to alter geometry. Arbitrary FDM build directions remain `unknown`:
a direction alone does not define a complete build frame.

Rigid sheet/frame placements use the evaluated authoring origin's local
producer evidence, identified by `geometry_feature_id`. Installed bounds do not
replace sheet thickness, bend radii or joint bead evidence. Missing native
geometry, missing inventories and unsupported imported evidence stay unknown.

## Explicit capabilities

The existing `manufacturing.process` object accepts these additional fields:

| Route | Fields |
| --- | --- |
| `sheet_metal` | `cutting_method:laser\|other`, positive `laser_max_thickness_mm`, positive `minimum_bend_radius_mm`, `process_reference` |
| `frame` | positive `minimum_weld_throat_mm`, `welding_procedure`, `process_reference` |

Existing common fields remain `material_grade`, `tolerance_mm`, `load_case`
and `finish`. References are caller text; provide the actual capability,
material/tooling or joint-design source and its revision. The tool does not
authenticate external procedure qualification. There are no invented universal
material limits or supplier defaults.

```json
{
  "expected_revision":12,
  "commands":[{
    "op":"feature.edit", "id":"Bracket",
    "manufacturing":{
      "route":"sheet_metal",
      "process":{
        "material_grade":"selected supplier grade",
        "cutting_method":"laser",
        "laser_max_thickness_mm":3,
        "minimum_bend_radius_mm":2,
        "process_reference":"actual machine/material/tooling capability revision 1"
      }
    }
  }]
}
```

Numbers are example caller inputs, not recommended limits. Manufacturing edits
replace the whole plan; include the retained settings. Invalid, cross-route or
unknown fields reject atomically, including on load. New fields reuse the
existing version 23 process storage; no `.ark` document version bump is needed.
Older runtimes may reject these fields and must not silently discard them.

## Evidence and scope

The `dfm` CTest covers process persistence and invalid input, agent and batch
access, real OCCT positive/negative FDM/CNC screens, unsupported FDM orientation,
actual sheet radius updates through a rigid placement, stamped blank rejection,
real exact bead throat comparisons, and retained read-only state. See the
[verification record](verification-dfm-2026-10-13.json).

C.03 remains **Parcial**. This is local rule and contract evidence. Slicer/CAM,
machine trials, supplier procedure attestation, physical material/strength
validation and production qualification are outstanding.
