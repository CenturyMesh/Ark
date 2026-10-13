# Test the 018B draft

Run the isolated preview:

```powershell
cd C:\Sw_templates\Ark
& .\out\test-preview\018B\ark-studio.exe
```

Open the synthetic example with associative datums, a curved 3D path,
varying rectangular sections, an exact polynomial and a constrained linked line:

```powershell
& .\out\test-preview\018B\ark-studio.exe --script .\examples\authoring-018B.arkops
```

Existing r1–r4 processes and directories are preserved. Prepare another copy of
a finished native build with `scripts/prepare-test-preview.ps1 -Name <new-name>`;
an existing preview directory cannot be overwritten.

| Workflow | Manual check |
| --- | --- |
| Face glow | Select a visible named face. Its boundary has an orange halo. Select another occurrence of the same part and confirm that only that occurrence is highlighted. Clear selection. |
| Editable sketches | Select a saved sketch → **Edit sketch**. Drag endpoints, circle radius and a whole wire. Verify snapping, retained parameter expressions, loop shading, Escape, Undo and Redo. Conflicting/stale edits must reject. Recipe entities use **Coordinates** or Tab to edit their source fields. |
| Construction geometry | Search **Offset plane**, **Plane at angle**, **Plane through axis and point**, **Axis through two points** and **Linked construction point**. Select a datum by its tree row or viewport geometry; use its eye to hide/show independently. Change its source and inspect the updated world origin/axes with `plane_inspect`. |
| Sketch menu | **All tools → Sketch entities** contains the rectangle/slot variants, perimeter circle, ellipse, conics, style spline, equation curve, points, centerlines, midpoint lines, text, route and belt curves. Source fields accept expressions. Text uses a builtin stroke engraving font. |
| 3D path and sweep | Create **Interpolating spline 3D**, two closed local XY section sketches, then **Sweep with varying sections**. Use stations `0, Start, 0; 1, End, 0`. Edit the path, section dimensions and roll; the section planes follow arc length and the exact tangent. |
| Spatial constraints | Select a segment curve → **3D sketch constraints**. Add distance, fixed, plane/axis incidence and direction constraints. Check residuals/free degrees of freedom with `native_inspect`. Change a named dimension, save/reload and try a conflicting edit. |
| Curves and surfaces | **Exact polynomial XYZ** retains exact coefficient geometry. General XYZ expressions use adaptive observed error checks; no bound between checkpoints is claimed. **Spline on exact surface** uses the selected face's UV domain. |
| Agent coordination | Parallel roles can reserve a short writing window at an observed revision, preview and apply, then release it. A busy peer receives the owner and remaining time; reads continue. Revision conflicts remain explicit. |
| Component sourcing | Ask for a specific bought component and real product/datasheet URL. Agents should search and read the product page. Unrelated results and empty JavaScript-only pages reject; supplier compatibility remains a separate review. |

The previous [017B controls](AUTHORING_017B_TESTING.md) remain useful for continuous
lines, dimensions, offsets, projection/trim, semantic jumps and BOM batch export.
Use copies for testing: documents that use the new source types require format 34.

The [draft PR](https://github.com/CenturyMesh/Ark/pull/4) stays in draft. It will
advance only after the user tests and explicitly requests that step.
