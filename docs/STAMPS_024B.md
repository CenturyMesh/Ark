# Stamps and regional appearance — 024B

This local authoring scope addresses **B.01**, **B.02**, **B.03** and **D.02**.
Those milestones remain **Parcial** beyond the measured scope. A Stamp applies
actual raster artwork to an editable plane or to named exact faces. Face
appearance assigns display colors and named materials to regions of one body.
Neither changes the production geometry or establishes a manufacturing process.

## Embedded artwork

Use `image_asset{action:"import",name,path,expected_revision}` to import the
actual PNG or JPEG created by a user, available image-generation skill or
connector. ARK imports a completed artifact; this operation does not itself call
an image provider or claim that a missing image has been generated.

Import fully decodes the pixels using the existing attachment decoder and checks
the portable PNG/JPEG decoder too. The original compressed bytes are embedded
once with their SHA-256, MIME type and decoded width/height. No linked file or
remote URL is needed after saving. Individual source files must fit 4 MiB,
8192 pixels per side and 16 megapixels. The **whole logical document retains its
4 MiB limit**, including base64 overhead and retained history; an image near the
individual file limit will therefore not fit in an otherwise valid document.
There are at most 64 named image assets and 128 Stamps per document.

`image_asset{action:"inspect"}` and ordinary `doc_info` return source hashes,
dimensions and names without base64 pixels. The complete authoring source saved
in `.ark` contains `image_assets[].data_base64`. A source-byte or metadata
mismatch rejects a load atomically. Removing an asset still referenced by a
Stamp, canvas or appearance also rejects. Replacing a named asset updates the
artwork for its applications while retaining their authored size and placement.

## A sketch-plane image

`canvas.set` retains its legacy path contract and accepts `asset` for a portable
embedded image. Choose a real datum name or `XY`, `XZ`, `YZ`, and give physical
dimensions in millimetres or parameter expressions:

```json
{
  "expected_revision": 7,
  "commands": [{
    "op": "canvas.set", "name": "Concept artwork", "asset": "Logo",
    "plane": "XY", "width": "logo_width", "height": "logo_width*0.6",
    "opacity": 0.8
  }]
}
```

The raster is a display application on that plane. It is not a sketch wire,
engraving, relief, vectorization or manufacturing contour. Legacy path-only
canvases remain readable, including unavailable files.

## A Stamp on geometry

Inspect the producer's current exact faces and
`native_inspect.topology.brep_sha256` before writing. The persisted recipe owns
its feature ID, semantic face names and observed digest. Supply that digest as
`source_topology`; numeric display triangle or face indexes cannot author a
binding. Named split-line regions such as `split.<digest-prefix>.region.N` use
this same contract.

```json
{
  "expected_revision": 8,
  "commands": [{
    "op": "stamp.set", "name": "Housing branding", "asset": "Logo",
    "feature": "Housing", "faces": ["z_max"],
    "source_topology": "<observed 64-character B-rep SHA-256>",
    "origin": [25, 20, 12],
    "u_axis": [1, 0, 0], "v_axis": [0, 1, 0],
    "width": "logo_width", "height": "logo_width*0.6",
    "depth": 0.1, "opacity": 1, "front_only": true
  }]
}
```

`origin` is the rectangle centre. For a feature target, origin and axes are in
that feature's local millimetre frame, so placed assembly occurrences retain
the same part artwork. `u_axis` and `v_axis` must be orthogonal unit vectors;
their cross product is the projector front normal. Physical width/height are
positive expressions up to 1e6 mm. The image bottom-left is UV `(0,0)`; +V points
up. Source PNG/JPEG rows are decoded from top to bottom and the renderer must
account for that row convention.

The projection has three explicit display boundaries:

- Only the selected exact faces can receive the Stamp.
- The image clips to UV `[0,1] × [0,1]` and to a symmetric slab within `depth`
  millimetres of the projector plane.
- `front_only:true` requires an outward triangle normal facing the projector
  normal. `false` permits both orientations within the same selected faces and
  slab; it does not select additional geometry.

Curved selected faces may receive the planar projection inside these bounds.
This is a bounded projection, not geodesic image wrapping or an automatic UV
unwrap. The CPU projection helper clips triangles at every rectangle/slab
boundary and publishes interpolated UVs for renderer and test use. It remains
display tessellation; B-rep geometry is unchanged.

For a plane Stamp, omit `feature`, `faces` and `source_topology`, and supply
`plane`. The same origin and orthogonal axes are then relative to the datum
frame. `stamp.set` replaces a same-name recipe; `stamp.remove{name}` removes it.

## One body, different regions

Use an exact split line to create face regions while preserving the physical
body, then inspect the new region names and digest. Apply a regional display
override with:

```json
{
  "expected_revision": 9,
  "commands": [{
    "op": "face_appearance.set", "id": "Housing",
    "face": "<observed exact region name>",
    "source_topology": "<observed exact B-rep SHA-256>",
    "appearance": {
      "base_color": [0.12, 0.3, 0.72], "roughness": 0.35
    }
  }]
}
```

`material` optionally names a document material. `appearance` is a partial
override using the existing color/metallic/roughness/texture fields; the new
`texture_asset` names embedded artwork. An appearance texture follows the
renderer's body texture coordinate convention; a Stamp provides explicit
physical projector placement. Regional materials affect display only: density,
mass and manufacturing intent remain body-owned. There are at most 128 face
appearances per feature. Independent face transparency sorting is outside this
scope, so a face's resolved opacity must equal the body's opacity. A differing
opacity rejects instead of rendering misleading transparent regions.

`face_appearance.set` replaces the recipe for that feature and semantic face;
`face_appearance.remove{id,face}` restores the inherited body appearance. To
remove a material still used by a region, clear or replace that region in the
same atomic batch first.

## Revision and topology behavior

All edits use `doc_apply` with the observed `expected_revision`. Source image,
projection and face validation happen on a scratch document. Geometry changes
must leave every bound face at its observed exact digest; stale, split, deleted,
obsolete or ambiguous bindings reject the complete batch. ARK does not silently
retarget artwork after a parametric geometry change. Remove the affected
applications before a geometry change, inspect the changed producer and author
new explicit bindings. Geometry-independent appearance batches retain the
evaluated B-rep rather than rebuilding it.

The new sources conditionally require document format **39**. Exact
`native_solid` `split_line` recipes use the same conditional gate. Existing
documents without these recipes keep their earlier source format; the 024B
application identity alone does not change their version.

Isolated design containers do not yet own or remap embedded assets, Stamps or
face appearances. Checkout, child authoring and merge explicitly reject these
sources with `CONTAINER_UNSUPPORTED_APPEARANCE`, including unused embedded
assets, instead of dropping or partially merging them. Author this appearance
work in the owning document. A bare exact split-line recipe remains supported
by the existing geometry container contract and retains format 39 at checkout.

## Verification and remaining scope

`tests/appearance_024b_tests.cpp` covers synthetic PNG/JPEG pixel decoding,
source-hash/dimension/MIME/truncation rejection, deterministic source roundtrip,
old path-only files, authoring copies, compact readback, atomic stale-revision
and invalid-projector rejection, clipped UV area, back-facing triangles and
depth bounds. The OCCT-enabled scope adds an exact face override and surface
Stamp, stale upstream geometry rejection, opacity restrictions and restored
source recomputation. The test runner and visual capture results belong to the
iteration's verification record; these test definitions alone are not evidence
that a target build passed.

General UV unwrapping, image-provider integration within ARK, vector engraving,
texture baking into STEP, printing process control, color calibration and
manufacturing qualification remain outside this local authoring contract.
