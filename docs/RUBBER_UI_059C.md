# 059C — rubber stock and local study interface

C.04 / B.01 / A.04: implemented for integrated local verification. This interface
does not qualify material properties, large-strain behaviour or manufacturing.

The Rubber panel creates editable exact belt, track, tire and ball stock through
native forms and one atomic revision-checked transaction. It declares a separate
material with the entered density and Young modulus. The visible defaults,
1100 kg/m3 and 3 MPa, are labelled illustrative; they are not a material database.
Stock dimensions remain CAD expressions. Creation adds an occurrence when the
document already has an assembly, without overwriting an existing material.

Study and Environment configure a selected smooth rubber definition. GPU and CPU
reference are explicit choices. Geometry with exact relief or downstream
modification requires a separate analysis recipe and is rejected. This panel
currently submits unpinned studies with ground contact; fixed nodes and pulleys
remain available through the explicit tool contract.

All analysis positions, gravity, ground height and initial translation use the
definition's local axes. The selected occurrence's placement only positions the
study for viewing; this is not an assembly contact solve. Cyan is the derived
surface network and the original dark stock remains exact CAD. Play, Pause,
previous/next frame, first/last frame, Fit study and Reset preview never apply
deformation or placement edits to CAD. Evidence includes the reported solver,
declared inputs, exact mass authority, reduced stiffness model and solver limits.

`physics_inspect` runs through the background read scheduler with
`include_frames:true`, source revision, document epoch and request generation.
The decoder also verifies the exact source SHA-256 and definition ID, bounded
node/frame/index counts, finite coordinates and complete ordered time endpoints.
Cancellation, document replacement, later edits and old callbacks cannot publish
playable stale results. Closing the panel hides the preview; cancellation remains
an explicit control for a running job.

Rendering uses a separate VAO, one dynamic packed vertex buffer and one static
index buffer. It uploads only when the displayed frame changes, does not pollute
the immutable CAD mesh cache, and does not touch CAD picking or display generation.
CPU normal generation and graphics uploads are display work; this note does not
claim all viewport preparation happens on the GPU or that GPU solving is faster
for every network size.

## Native verification

`rubberSelfTest059(dc, output, false)` exercises actual control clicks and native
forms for all four stock types, declared material, study/environment Apply and
Cancel, play/pause/step/fit/reset, stationary upload reuse and source immutability.
Synthetic transport adversaries cover cancellation, late callback, wrong source
hash, invalid indices, document epoch and revision changes. Synthetic frames are
explicitly labelled and are not physics evidence.

`rubberSelfTest059(dc, output, true)` uses the production scheduler and both explicit
CPU/GPU solvers, captures the final derived mesh, and records the native reports.
Receipts use `.rubber.json`; captures include `.rubber-create-Belt.png`,
`.rubber-create-Track.png`, `.rubber-create-Tire.png`, `.rubber-create-Ball.png`,
`.rubber-study.png`, `.rubber-environment.png`, `.rubber-running.png`,
`.rubber-final.png`, `.rubber-evidence.png`, `.rubber-reset.png` and
`.rubber-stale.png` for the transport mode.

Compilation, execution and screenshot inspection are pending the integrated 059C
build. No user drawing is used in this fixture.
