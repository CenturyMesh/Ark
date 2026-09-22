# Straight tube STEP with retained-wall notches

The intended product workflow is: open the final STEP, reconstruct its editable geometry, derive a straight 3D tube with laser-cut notches that retain one hinge wall, and verify refolding against the complete source. A flat wall DXF is a different output. The current release adds a **geometric preview planner and exact STEP export bridge**, not yet an automatic validated inverse of an arbitrary STEP.

## Implemented proof

`notch_plan` is a pure C++ MCP operation. It accepts rectangular stock dimensions, explicit hinge-axis offset within the retained wall, notch stations, angles and retained sides (`bottom`, `top`, `left`, `right`). It rejects invalid dimensions, notches at stock ends and overlapping axial envelopes.

In a local transverse section, let `h` be the assumed hinge-axis offset, `y` the depth from the retained outer wall, `s` the station and `a=angle/2`. The two cut planes satisfy:

```
x_left  = s - (y-h) tan(a)
x_right = s + (y-h) tan(a)
```

Rotating the left and right rigid halves by `-a` and `+a` about the hinge maps both cut faces to the same plane. The identity is checked numerically, including its inverse. The cut starts at `y=wall`, so the complete nominal wall below it remains present in the straight blank.

The removed volume is independently integrated over two side-wall strips and the opposite wall. The optional OCCT bridge constructs the hollow stock, subtracts the C++-generated cutter prisms, verifies the retained wall by solid intersection, and compares the B-rep volume with the analytical integral. It then writes STEP, reads it back, checks one valid solid and measures the Boolean symmetric-difference volume.

This proves the implemented cut geometry under an **ideal zero-radius hinge** assumption. It does not prove physical strain, springback, weld fit or manufacturability of a finite-thickness wall. The hinge position is an explicit input, not a calibrated neutral-axis claim. All plans and receipts carry `manufacturing_authoritative:false` and `source_reconstruction_verified:false`. The STEP is an engineering preview, not an approved machine program.

## Reproduce the preview

Build ARK, then use a Python environment containing the optional [OCCT bindings requirements](../scripts/requirements-occt.txt):

```powershell
.\scripts\build.ps1 -Test
python scripts/export_notch_step.py examples/notch-preview.request.json out/notch-preview.step --engine out/fabrication/ark-mcp.exe
```

This is one command from request to STEP. The native C++ process creates the plan; the small Python adapter calls OCCT's C++ B-rep/STEP APIs. It also writes `.arkcut` intent and `.validation.json` evidence. Native ARK geometry and the UI do not require Python or OCCT. The adapter has been tested with `cadquery-ocp==7.8.1.1.post1`.

The checked-in [example STEP](../examples/notch-preview.step) is a 600 mm long, 40×30×2 mm nominal rectangular tube with two opposite-side notches, at 60° and 90°. It is a synthetic example and **is not a reconstruction of SKT01.DCB109**. Its receipt records:

- One valid connected B-rep solid.
- Remaining volume: approximately 146740.226810 mm³.
- Error against analytical removed-volume integration: approximately 8.73e-11 mm³.
- Retained-wall volume error: approximately 9.09e-13 mm³.
- STEP roundtrip symmetric-difference volume: 0 mm³ in the test.
- Rigid cut-face closure residual: approximately 7.11e-15 mm.

The figures are floating-point residuals for this example, not process tolerances or a physical accuracy claim. Native tests cover four hinge sides and multiple angles; the OCCT tests cover all four sides, STEP readback and rejection of altered cutter data.

## Audit of the provided STEP

The screenshot's file was located at `C:/test/SKT01.DCB109.STEP`. It was read without modifying the source. Its SHA-256 is `cca7d098bf4fdede3d041fd61dde33282628262fcecfa5f3153d2979309cecec`.

OCCT reports millimetre source units, a valid B-rep, 125 planar faces and 109 cylindrical faces. It contains two solids: a principal body of approximately 206742.440180 mm³ and a small fragment of approximately 0.000144455 mm³. The fragment was retained in the audit. A cylindrical face count is not a hole count; feature recognition has not yet been proved for this source.

Reproduce the inventory with:

```powershell
python scripts/step_audit.py C:/test/SKT01.DCB109.STEP out/SKT01.audit.json
```

The original CAD is not checked into the repository. Full local audit data and the initial import/export investigation remain under `out/source-assessment`.

## Acceptance gates for automatic source reconstruction

Before a source-derived blank may be marked accepted, the implementation must:

1. Preserve a hash and complete solid/face/feature inventory of the source, including small fragments, holes and corner radii. Any removal needs an explicit modeling decision.
2. Recover the section, segment frames, hinge locations and feature attachments with per-entity evidence. Ambiguities remain unresolved rather than being silently replaced by plausible geometry.
3. Derive the straight cut geometry and a forward folding model from the same declared physical/geometric assumptions. Native part geometry, its preview mesh and manufacturing evidence remain distinguishable.
4. Refold and compare the complete boundary in both directions against the source using tolerances declared before reconstruction; verify topology, feature positions, clearances and collisions in addition to volume.
5. Qualify the material/hinge model and cutting process before asserting manufacturing readiness. Kerf, ligament radius, thinning, strain and springback are not determined by the external shape alone.

The automatic source-recognition/refolding chain and physical qualification are still open work. The new preview planner deliberately does not label those gates passed. GPU acceleration is secondary to a correct source contract.

## Integrated Studio workflow (0.5)

The preview is now available as an editable `notched_tube` in Studio, including bend-point dialogs, viewport markers, sequential animation, `.ark` save/load and an **Export straight STEP** button. The original physical/source-reconstruction limits above still apply. See [BEND_WORKSPACE.md](BEND_WORKSPACE.md).
