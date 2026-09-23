# Native sheet metal in ARK 0.9

The sheet engine is C++ using OCCT 8.0.1. Authored sheets and imported sheets
have separate contracts. Display meshes are derived from their exact B-reps;
STEP export uses the B-rep, never a reconstruction from triangles.

## Authored sheet: a base and a tree of flanges

Create a base in **Commands > Sheet metal**, then add flanges and openings from
forms. Existing L-bend documents remain editable and promote to the same panel
program when a graph operation is applied. Everything is C++; OCCT 8.0.1 creates
the formed and developed solids.

An agent can create a complete bracket in one atomic `doc_apply` batch:

```json
{"commands":[
  {"op":"sheet.create","name":"Bracket",
   "p":{"width":60,"thickness":2,"k_factor":0.42},
   "base":{"id":"base","length":80}},
  {"op":"sheet.flange.add","id":1,"flange":{
   "id":"back","parent":"base","edge":"u_max","length":30,
   "width":"edge_length-10","offset":5,"inner_radius":4,"angle":90}},
  {"op":"sheet.opening.add","id":1,"opening":{
   "id":"mount","panel":"back","kind":"circle",
   "x":"panel_length-10","y":"panel_width/2","radius":3}}
]}
```

The ID `1` assumes a fresh document. Use returned `created_ids` and
`sheet_edits[].entity_id` in subsequent edits. `sheet.base.edit{id,values}`,
`sheet.flange.edit{id,entity,values}` and `sheet.opening.edit{id,entity,values}`
change expressions while retaining identities. Removing a flange with dependent
panels/openings requires explicit `cascade:true` or removing its dependencies in
the same transaction. Invalid edits preserve the old document and revision.

Each panel has U (length), V (width), and a material normal. Base U is world X,
normal is world Y, and V is world Z. Flange edges are `u_min`, `u_max`, `v_min`,
`v_max`. A child's inbound `u_min` is reserved. One flange may occupy each edge;
`width` and `offset` locate a narrower attachment on that edge, making corner
clearance explicit. Child U points outward and its V follows the attachment.

Limits: 16 flanges, 128 openings, positive lengths/radii/thickness, K in `[0,1]`,
and `0.001 <= abs(angle) < 170` degrees. Negative angles bend to the opposite
side. Parents precede children. No implicit corner reliefs or bend-crossing
openings are invented. Nonadjacent contact and positive material overlap are
rejected in both flat and formed poses. This domain covers rectangular panel
trees, not hems, arbitrary freeform panel boundaries or continuous double curvature.

Circle and polygon openings are exact cuts in a panel's local frame. `x` is U,
`y` is V. Their local expressions can use `panel_length`, `panel_width` and
`thickness` as well as document parameters. Flange dimensions can use
`parent_length`, `parent_width`, `edge_length` and `thickness`. Openings must stay
inside their panel and away from edges/bends. Exact cutter clearance permits
non-overlapping shapes whose bounding envelopes overlap.

`native_inspect{id}` returns panel frames, bend allowance, opening identities and
centres, independent analytic volumes and refold evidence. The engine constructs
formed bends as differences of cylinders, then independently refolds the flat
panels around their neutral lines and reconstructs bends by revolving a wall
section. It checks panel frames and whole-solid symmetric difference, in addition
to validity, connectivity and exact component interference.

Studio **Flat / Formed / Play** and its slider show the sequential bend preview.
`sheet_preview{id,progress}` exposes the same panel poses to agents. Preview is
read-only: STEP export always uses the explicit exact flat or formed B-rep.
Endpoint interference is checked; the animation does **not** certify the motion
trajectory or press-tool accessibility.

The original `sheet_metal` parameters `{width,leg_a,leg_b,thickness,inner_radius,
angle,k_factor}` and numeric opening panels `0/1` still reproduce old documents.
New graph features store `native:{base,flanges,openings}` and the global width,
thickness and K factor in `p`. `.ark` version 7 preserves this authored intent.

## Import an existing STEP sheet

`step_inspect{path,faces?}` inventories source solids, units, B-rep validity,
dimensions, volumes and surface types without modifying the document. Import
using `import_sheet_step{path,k_factor,body_index?,root_panel?,replace?}`.

The supported source domain is deliberately explicit:

- A uniform flat sheet; the thickness must be no more than one quarter of the
  smaller in-plane extent for this eligibility check.
- Or one connected solid with two uniform planar flanges, one complete paired
  cylindrical bend, a constant axial width, and an angle below 180 degrees.
  The bend width must be at least four times its thickness.
- The two developed flanges must share the same thickness and width envelope.
  Planar perimeter cutouts and internal through-openings on the rigid flanges
  are carried by their original B-reps. Pockets and countersinks are outside
  the uniform-panel domain.

Multiple solids require an explicit zero-based `body_index`. When one bend is
present, the larger rigid-panel volume defines the fixed flange if unique;
equal candidates require `root_panel:0` or `1`. These indices are local to the
specific source version, not persistent face names across changed files.

Segmented cylindrical skins, multiple bends, hems, junctions, incomplete or
ambiguous contacts, and cuts crossing the deforming bend are rejected. An
unsupported sheet is not replaced by a nominal rectangle or approximate mesh.
The imported feature is read-only. Reimport a changed STEP to create a new
source-owned result.

## Neutral factor and mathematical checks

`k_factor` is a required caller input, in `[0,1]`. It is not inferred from the
filename, image, material name or an external reference drawing. For angle
`theta` in radians, inside radius `R` and thickness `t`:

```text
neutral radius = R + K*t
bend allowance = abs(theta) * (R + K*t)
```

This is a declared constant-thickness geometric development model. It does not
estimate material calibration, springback, thinning or forming strain. Its
flat and formed volumes are equal at `K=0.5`. For another K, the expected
geometric volume difference is explicitly checked and reported:

```text
formed volume - flat volume = sum(abs(theta_i) * width_i * t*t * (0.5 - K_i))
```

ARK never labels that difference as conserved material. In particular,
`geometric_material_conservation` is false for a bent sheet with non-midwall K.
`manufacturing_authoritative` remains false for all results.

The source pipeline snapshots the original STEP bytes and binds their SHA-256.
It proves opposed inner/outer material skins and complete tangent contact,
extracts the actual rigid flanges, and verifies that each is a uniform
extrusion of its source planar skin. Exact rigid transformations carry those
flanges and their openings into the flat state. Developed material must not
overlap. The measured bend-band volume supplies an independent developed
length for refolding; the rebuilt bend and transformed flanges must recover
the whole original selected solid within the reported symmetric-difference
volume tolerance.

Opening checks preserve complete source wires, their perimeter and unique
rigid-panel ownership. Inverse-transform and boundary-distance samples are
reported with a 1e-6 mm numerical tolerance. The opening inventory counts one
lower-skin mouth per through-opening; preservation of all rigid material is
also checked by the whole-source comparison.

## Inspect, view and export

Use `native_inspect{id}` for the compact geometric report and opening
inventory. Studio offers **Formed** and **Flat** views; switching view does not
edit parameters or document revision. Authored panel graphs also have a read-only
sequential bend preview; imported source sheets keep static formed/flat views.

`export_step{id,path,pose:"flat"|"formed"}` defaults to flat for sheet features.
It writes the selected B-rep, reads it back and emits a validation sidecar.
For an imported sheet, an existing original file with changed bytes blocks
export. A saved source package remains portable when that original file is
unavailable, because it contains the verified B-reps and their hashes.

`examples/sheet-metal.ark` and its `.arkops` replay demonstrate the authored
workflow. The independent C++ source tests cover flat stock, circle and polygon
openings, a rotated source, explicit multiple-body selection, non-midwall K,
source/refold agreement, rejected bend holes, changed source and payload hashes,
and STEP readback. This is the first bounded native domain, not a claim that all
STAGE-supported sheet families have been ported.

## What was reused from STAGE and TRACE

The donor audit is in [STAGE_NATIVE_ASSESSMENT.md](STAGE_NATIVE_ASSESSMENT.md).
ARK reuses the reasoning contracts: original-source ownership, explicit body
selection, paired material-skin proof, declared allowance, independent
reconstruction, and honest rejection. It does not embed STAGE's Python pipeline
or use TRACE nesting output to decide source geometry. No donor checkout was
modified and no Python runtime or test gate remains in ARK.

The 0.9 multi-flange example is `examples/sheet-flange-tree.ark`, with its
`.arkops` replay and exact `-flat.step` / `-formed.step` exports. Its regression
checks signed bends on all four panel edges, exact opening clearance, panel
identity under edits, neutral-line refold, STEP readback and atomic undo.
