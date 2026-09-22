# Source STEP tubes with original openings

ARK can now import a supported STEP tube directly, retain its original hole and cutout geometry, display the formed and straight states, and export the straight STEP. This is a separate `source_tube` feature; it does not remake the source as a generic empty tube with guessed holes.

## Workflow

Use **Import STEP** in Studio. The existing unsaved-document guard runs before replacing the current document. A verified import initially shows the formed source. **Straight**, **Folded**, **Play** and the slider use the recovered finite-radius hinges. Click an opening in the inspector to focus the camera on the actual cut geometry. **Export straight STEP** includes the source openings and cutouts, independently of the displayed pose.

Imported geometry is read-only. Stock/bend dialogs intended for newly designed laser tubes are not applied to imported source geometry. Reimport a modified STEP to change the source. Saving `.ark` preserves the preview, projection evidence and the verified exact straight STEP payload, so export does not depend on an old cache directory.

The source STEP and project-specific derived artifacts stay local; they are not included in the repository. The local task artifacts are under the ignored `projects-local` directory.

## What the geometry pipeline does

1. Hash and snapshot the input bytes before reading STEP. Reject an input that changes during import. Read the complete B-rep, including additional small solids.
2. Recognize an unambiguous family of parallel finite hinges from paired coaxial cylindrical faces. Match radius, angular interval, axial width and exact analytic boundary types. Reject holes/trims in a deforming hinge that the rectangular-band model cannot represent.
3. Remove the measured annular sectors and recover the rigid spans. Hinge endpoints attach through complete radial interface areas; no nearest-vertex assignment is used.
4. Build the chain and unfold each rigid B-rep by an isometric transform. The endpoint nearest the source-coordinate origin defines the initial flat datum; the full source-to-flat matrices record that choice. Its original planes, rounded section corners, drilled bores, polygonal openings, chamfers and edge cutouts travel together. OCCT's [shape-transform operation](https://occt3d.com/dev/doc/refman/html/class_b_rep_builder_a_p_i___transform.html) applies the transformation to supporting curves and surfaces.
5. Replace each curved ligament with a straight band using `L = angle * (inner_radius + outer_radius) / 2`. This is an explicit mid-wall, volume-preserving geometric model. It is not a material-specific neutral-axis or springback calibration.
6. Verify a common straight cross-section envelope. Additional solids must have a unique source attachment and remain present; ambiguous ownership is rejected.
7. Assign each opening contour to its rigid span. A face shared by several spans is resolved using full common edge length of the contour, never its centroid. Retain source/flat centers, oriented normals, radii where applicable, perimeters and source face IDs.
8. Refold with independently composed finite-radius transforms and reconstructed annular sectors. Check material conservation and Boolean symmetric difference against the complete input, including extra solids. Write STEP, read it back, and check the result again.

The B-rep checks use [OCCT validity analysis](https://occt3d.com/dev/doc/refman/html/class_b_rep_check___analyzer.html). The viewport mesh is only a tessellation, with 0.025 mm display deflection and 0.12 rad angular tolerance; it is not the authority for the exported geometry.

## Openings and projection evidence

The opening inventory counts closed inner wires on planar **outer** tube walls. Opposite-wall openings are separate entries. Inner mouths and the hollow tube's end caps are not extra drilled openings in this count. Geometry outside this inventory is still retained as part of the complete rigid B-reps and included in whole-source comparison.

Reports include contour perimeter conservation, sampled projected-edge distances, inverse coordinate residuals, fold-transform consistency, source/refold symmetric-difference volume, source/flat volume and STEP readback difference. Linear checks use a declared 1e-6 mm numerical tolerance; volume checks use `max(1e-5 mm³, source_volume * 1e-8)`. A zero Boolean difference is the observed kernel result, not a claim of zero physical manufacturing error.

STEP export also writes `.validation.json` and `.openings.csv`. The CSV contains source and flat coordinates for audit; it is not a replacement for the actual contour geometry in STEP. The report includes source SHA-256, importer code SHA-256 and OCCT binding version.

## Supported domain and limits

The current automatic route supports a chain of planar tube spans connected through complete cylindrical retained-wall bands. It is not an unrestricted inverse-forming solver. Ambiguous hinge pairs, branches, unsupported hinge boundary curves, holes crossing deforming bands, inconsistent straight envelopes and unassigned extra solids fail closed.

`manufacturing_authoritative` remains false. A geometrically verified projection does not certify material strain, thinning, tooling, cutting kerf, springback or weld fit. Those require process data and separate validation.

## Agent interface

- `import_step{path,replace?}` creates the verified source feature atomically. Failed recognition leaves the document unchanged. Studio uses `replace:true`; the default API behavior adds a feature.
- `source_inspect{id,progress?}` returns opening coordinates and the projection report without transmitting large mesh/B-rep buffers. Optional progress returns finite-hinge display transforms.
- `export_step{id,path}` exports the exact straight source projection and its audit sidecars. Source-file drift or altered stored STEP bytes are rejected.

There are 22 MCP tools. `.ark` version 4 embeds source preview data and a base64 copy of the verified straight STEP, protected by its SHA-256. Older document versions remain readable. The current 4 MiB document limit still applies; large source assemblies are not yet a packaged external-asset format.

The optional OCCT worker setup is shared with [BEND_WORKSPACE.md](BEND_WORKSPACE.md). The input limit is 64 MiB and the Windows worker has a 30-second process-tree limit. Derived caches are local and ignored by Git.

## Reproducible verification

```powershell
.\scripts\build.ps1 -Test
# Use a Python with scripts/requirements-occt.txt installed:
python tests/source_projection_test.py
python tests/source_step_integration.py out/source/ark-mcp.exe path/to/source.step
.\out\source\ark-source-tests.exe path/to/saved-source.ark
```

The independent synthetic fixture uses different stock dimensions, a 70-degree hinge, circular and hexagonal openings, and a partial-cylinder Boolean construction. A rotated/translated regression checks the flat opening coordinates under the same end datum. Reversing the selected endpoint changes the presentation datum, while source/refold checks still use the recorded transforms. A hole through the curved ligament must be rejected instead of disappearing. Native C++ tests also check hinge/rigid interface continuity and verify the displayed source opening coordinates after folding.

The supplied real part is assessed locally with full source/refold comparison and readback. Customer geometry and screenshots are not used to tune production tolerances or checked into this repository.
