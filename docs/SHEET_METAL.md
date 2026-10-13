# Native sheet metal in ARK 0.9

The current authored-sheet extension adds exact `slot` and rotated rectangular
`window` openings, expression-driven opening patterns, `sheet_inspect`, and
analytic `export_flat_dxf`. The **SHEET METAL** ribbon exposes authoring and
global Formed/Flat/Unbend views. See the [KITE car showcase](KITE_SHOWCASE.md).

`sheet.opening.pattern` takes `id`, an `opening` template, integer `count`
from 2 to 32, and expression offsets `dx`/`dy`. It creates named openings and
returns their `entity_ids`; overlap still rejects the whole transaction.
Sheet editing commands accept a numeric feature ID or its unique name.

An exact slot has `kind:"slot", x, y, length, width, angle?`, where length is
the overall length. A window has `kind:"window", x, y, width, height, angle?`.
Angles are degrees. Coordinates stay in the owning panel's UV frame.

`sheet_inspect{id}` returns a compact bend table, opening/panel inventory,
flat bounds and a flat B-rep digest, plus refold evidence. `export_flat_dxf{id,path}`
sections the validated authored flat solid analytically and writes LINE, ARC
and CIRCLE contours with separate BEND lines in millimetres. Contour extents
are checked against the exact blank and stale digests reject. No sampled mesh
is promoted into a manufacturing contour. DXF remains geometric development
under the declared K factor, not a material/process qualification.

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
rejected in both flat and formed poses. The flange domain covers rectangular panel trees. Open round hems and bounded
circular emboss stamping are separate authored operations described below;
arbitrary freeform panel boundaries and continuous double curvature are outside
this domain.

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
New graph features store `native:{base,flanges,hems?,openings,stamps?}` and the global width,
thickness and K factor in `p`. `.ark` version 7 preserves this authored intent.

## Open round hems and circular emboss stamping

These are native OCCT authored operations with stable identities, editable
expressions and atomic rollback. They remain `sheet_metal` features and save
as document version 21 when a hem or stamp is present. Older sheet documents
keep their existing version. These bounded operations do not establish support
for closed/crushed hems, teardrop hems, arbitrary die profiles or deep drawing.

```json
{"commands":[
  {"op":"sheet.hem.add","id":"Bracket","hem":{
    "id":"rim","parent":"back","edge":"u_max","length":12,
    "inner_radius":3,"direction":1}},
  {"op":"sheet.stamp.add","id":"Bracket","stamp":{
    "id":"emboss","panel":"base","kind":"circular_emboss",
    "x":"panel_length/2","y":"panel_width/2",
    "radius":12,"top_radius":6,"depth":4}}
]}
```

A hem is a terminal return panel joined by a signed **180 degree** annular
cylindrical bend. `inner_radius` must be positive: the facing skins retain an
open gap of twice that radius. `direction` defaults to +1 and accepts only
+1 or -1. `length`, optional `width`, `offset` and `k_factor` have the same
expression/attachment meaning as a flange. The inbound edge and an occupied
parent edge remain reserved. A hem cannot have child flanges, openings or
stamps. Up to 16 flanges and 16 hems are accepted; actual material overlap and
unrelieved nonadjacent contacts reject. Flat allowances use pi*(R+K*t).
Independent neutral-line refold, exact whole-solid comparison and analytic
volumes also cover the 180 degree bend. The display trajectory is not certified.

A `circular_emboss` stamp replaces a circular portion of one rigid panel with
an exact solid of revolution: a conical wall, horizontal circular plateau and
sharp **mitred** transitions. It is a nominal authored geometry, with no inferred
die radius. The two conical skins are separated by the declared thickness in
the normal direction; the planar skins are separated by that thickness as
well. Mitred corner geometry is explicit. Positive `depth` raises the plateau
along the panel's material normal; a negative value lowers it.

The midwall outer transition is `radius`; the plateau midwall transition is
`top_radius`. The replacement footprint has radius `radius+thickness`.
Require `top_radius>thickness`, `radius>top_radius+2*thickness`, and
`thickness<abs(depth)<=radius-top_radius`. Coordinates and dimensions accept
expressions using `panel_length`, `panel_width`, `thickness` and document
parameters. The whole footprint must lie strictly inside its panel and clear
all openings and other stamp footprints. Up to 32 stamps are accepted, on the
base or ordinary flange panels. Exact formed-solid interference remains checked
against other panels and bends. The revolved section's analytic first moment
independently checks its OCCT volume.

`sheet.hem.edit` and `sheet.stamp.edit` take `{id,entity,values}`;
`sheet.hem.remove` and `sheet.stamp.remove` take `{id,entity}`.
Omitted identities are allocated from the shared persistent entity counter.
Removing an ordinary flange that owns hems or stamps requires explicit
`cascade:true` or removal of its dependent entities. Failed geometry edits
preserve both document and revision; undo restores the authored program.

`sheet_inspect` reports `hem_count`, bend `kind:"open_round_hem"`,
`stamp_count`, stamp dimensions/centres, wall model and analytic volume delta.
For a stamped sheet, **`flat_pattern_qualified:false`** means the flat B-rep is
only a nominal unstamped reference. Strain, blank size, thinning, springback,
material conservation and tooling accessibility have not been evaluated.
Flat STEP/IGES and DXF exports reject such a reference; explicit formed STEP
or IGES remains exact native geometry. The report's
`refold_symmetric_difference_mm3` is null when stamps are present:
`rigid_panel_pose_symmetric_difference_mm3` and
`rigid_panel_refold_verified` attest only rigid panel placement, not unfolding
of the embossed material. Manufacturing authority remains false.

Stamped `sheet_preview` accepts exact endpoints 0 (nominal flat reference) and
1 (actual formed geometry including stamping). Intermediate values reject
because forming strain is not modeled. Sheets without stamps retain the full
sequential bend/hem display preview.

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
and STEP readback. This is the first bounded native domain; broader sheet
families require separate qualification.

## Geometry and verification principles

The historical [native assessment](STAGE_NATIVE_ASSESSMENT.md) records
original-source ownership, explicit body selection, paired material-skin proof,
declared allowance, independent reconstruction and explicit rejection.
Production geometry is derived from the original exact source. ARK's native
sheet implementation and tests use C++, with no Python runtime or test gate.

The 0.9 multi-flange example is `examples/sheet-flange-tree.ark`, with its
`.arkops` replay and exact `-flat.step` / `-formed.step` exports. Its regression
checks signed bends on all four panel edges, exact opening clearance, panel
identity under edits, neutral-line refold, STEP readback and atomic undo.
