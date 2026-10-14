# Testing 022B authoring

Runtime identity comes from `release.json` (022B, 0.9.23). This iteration targets
A.01/A.03/A.04, B.01–B.05, C.01, D.02/D.04 and E.01. Native and viewport evidence
is local; manual acceptance and merge remain pending.

Build with the repository's native build workflow, or use the immutable preview
prepared by `scripts/prepare-test-preview.ps1`. Launch the `ark-studio.exe` inside
that preview. Keep your working designs outside the checkout and retain their
last saved copies while testing a draft.

| Check | Action | Expected behavior |
|---|---|---|
| Existing extrusion | Draw a closed sketch, drag its handle and release; drag the resulting handle again | The handle moves to the solid's end cap and reads **Drag to resize**. The same feature is edited, with one physical output. The sketch's original plane remains the source. |
| Explicit face extrusion | Select a planar face, then drag its handle | Creates an explicit face-sourced extrusion; selecting a face is a deliberate new extrusion operation. |
| Responsive preview | Drag rapidly; release while the exact preview is pending | The dimension updates immediately, native work runs separately, and only the latest valid recipe commits. **Esc** cancels publication. A new press cancels an earlier pending release. |
| Expressions and Undo | Press **Tab**, enter a named expression, Apply, Undo | The source expression persists. Apply reuses the validated geometry; Undo restores authoring. |
| Native vertex | Select **Pull vertex (CAD / mesh)**, click near a box corner, drag or enter X/Y/Z with Tab | A source-linked exact polyhedral operation pulls that topological vertex. Affected nonplanar quads become explicit planar facets. Curved, holed or unsupported faces reject. |
| Mesh vertex | Use the same vertex tool on an indexed mesh | Retains source vertex indices and topology fingerprint; no conversion to a B-rep is implied. |
| Component patterns | **All tools → Component patterns** | Linear, circular, pattern driven, sketch driven, curve driven and chain patterns have editable fields. Source edits recompute placements; skips retain stable copy slots. |
| Sheet programs | **All tools → Advanced sheet metal** | Miter/straight-edge contour flanges, corner operations, edge rips, paired tabs/slots and a declared bend table have typed source editors and atomic rejection. Inspect evidence after changes. |
| Manufacturing sheets | **Manage → Drawings** | Opens a sheet in the existing CAD viewport. Opening discovers sources and does not create a drawing. |
| Manual sheet creation | Choose **New** or **All sources**, then Parts/Assemblies | Creates explicitly requested source-linked sheets with editable precision/blueprint templates, dimensions and section views. Tolerances stay undeclared until entered. |
| Drawing/3D association | Click a projected curve, then **Design** or Esc | Selects the actual source definition/occurrence in the model. Source edits show stale measurements until explicit Refresh. |
| Agent behavior | Ask an agent for a model without requesting drawings | The agent must not create, edit, refresh or export 2D drawings automatically. A separate explicit drawing request enables that workflow. |

Pattern and sheet contracts are in [specialized patterns](SPECIALIZED_PATTERNS_022B.md)
and [advanced sheet metal](ADVANCED_SHEETMETAL_022B.md). Exact vertex scope is in
[native vertex pull](NATIVE_VERTEX_PULL_022B.md); manufacturing sheets are in
[the integrated drawing viewport](DRAWING_VIEWPORT_022B.md).

Cancellation invalidates the pending recipe and publication immediately; it does
not interrupt an OCCT operation already running. Native allowances are bounded
per recompute, not a global limit across simultaneous documents. Exact geometry
uses CPU OCCT; retained OpenGL buffers render previews. See the measured inputs,
failed attempts and limits in [the performance audit](PERFORMANCE_AUDIT_022B.md).

Automated native checks include `direct-preview-worker`, `native-request-budget`,
`authoring-022b-contracts`, `specialized-component-patterns`, `advanced-sheetmetal`,
`native-vertex-pull`, `associative-drawings` and `reactivity-activity`. Windows
viewport checks use `--self-test-direct-authoring-ui` and
`--self-test-drawing-viewport-ui` with a fresh `--settings-file` and an explicit
`--shot` prefix. These checks create only synthetic fixtures.
