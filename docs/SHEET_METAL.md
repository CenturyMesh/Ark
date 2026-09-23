# Native sheet metal in ARK 0.8

The sheet engine is C++ using OCCT 8.0.1. Authored sheets and imported sheets
have separate contracts. Display meshes are derived from their exact B-reps;
STEP export uses the B-rep, never a reconstruction from triangles.

## Authored sheet

Create a `sheet_metal` feature through Studio **Commands > Sheet metal**, or:

```json
{"op":"feature.add","type":"sheet_metal","name":"Bracket","p":{"width":"60","leg_a":"80","leg_b":"50","thickness":"2","inner_radius":"5","angle":"90","k_factor":"0.5"},"native":{"openings":[{"panel":0,"kind":"circle","x":"25","y":"20","radius":"4"},{"panel":1,"kind":"polygon","points":[[15,20],[25,20],[25,30],[15,30]]}]}}
```

Lengths are millimetres. `leg_a` and `leg_b` are straight tangent lengths, not
overall outside dimensions. `angle` is in degrees and currently must be
between 0 and 170. The two flanges share a width and thickness. This authored
feature currently contains one cylindrical bend.

Opening coordinates lie in each panel's own frame: `x` runs from the first
panel's free end toward the bend, and from the second panel's bend toward its
free end; `y` runs across the sheet width. Circle and polygon openings must be
fully inside one rigid flange, must not overlap, and cannot cross the bend.
Coordinates and dimensions accept parameter expressions. The generated solid
and blank each undergo independent analytic-volume checks.

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
formed volume - flat volume = theta * width * t*t * (0.5 - K)
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
edit parameters or document revision. Sheet animation is not implemented in
this release; the existing tube animation remains available.

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
