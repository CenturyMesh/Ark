# Native modeling in Studio

Open **Commands** (or press **Ctrl+K**) to create OCCT features in the active
`.ark` document. These commands use the same `doc_apply` feature history as the
agent connection; undo, redo, save, and live MCP operate on that document.

**Native surfaces** contains Loft sketches, Fill closed sketch, NURBS surface,
and Thicken selected surface. Loft lists two or more saved sketch names in
section order, separated by semicolons. Solid and Ruled are explicit choices.
Fill uses a saved closed sketch. The NURBS editor starts with a 4 by 4 pole grid
in JSON; edit `[x,y,z]` poles, optional `weights`, and the two degrees.
Thicken acts on a selected native loft, fill, or NURBS surface.

**Sheet metal** contains Create L bend and Import sheet STEP. The L-bend form
keeps width, both legs, thickness, inner radius, bend angle, and K factor as
editable expressions. The STEP importer asks for an explicit K factor, then
opens a Unicode file picker. It loads a source sheet as read-only geometry in
a new document. A zero-based body index and root panel choice can resolve
ambiguous source bodies when needed. The source geometry can be inspected and
exported, but editing its construction parameters is not offered. The inspector
shows the source projection proof and refold volume difference. Source STEP
export checks the recorded source digest when the original file is still
present; a changed source must be imported again.

Select a sheet to use the **Formed** and **Flat** buttons in the inspector. The
buttons switch the displayed mesh and picking data without changing the
document or its revision. The inspector reports the OCCT B-rep version,
validity, area, volume, opening count, and available source diagnostics.
The viewport is a tessellated preview; the B-rep is the source for native STEP
export.

Select a native sheet or surface and choose **Export native STEP** from
Commands or the inspector. A sheet export asks for **flat** or **formed** and
defaults to **flat**. Surface exports use their exact native B-rep. The
existing laser and source tube straight-STEP controls remain separate.

The sheet display toggle represents two validated poses. It does not animate
a bend or certify a manufacturing process. The K factor is a declared design
input for flat development, not an inferred material calibration.

For an isolated native UI check, run `ark-studio.exe --shot <temporary-png-path>
--self-test-native-ui`. This exercises the sheet form, formed/flat display,
flat STEP export, source sheet reimport, and NURBS editor without Python. The
generated flat STEP is written beside the chosen PNG as
`<temporary-png-path>.flat.step`. This opt-in check needs a Windows OpenGL 3.3
context and is separate from the headless CI CTest gate.
