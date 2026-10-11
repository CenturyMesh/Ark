# Joint-aware assembly manipulation

Milestones **B.01**, **B.04** and **D.04**: inspect interiors and manipulate an
editable assembly through persistent, revision-checked contracts. This is an
ideal rigid kinematic model; collision, forces and manufacturing qualification
require their own evidence.

`agent_onboard{topic:"assembly"}` describes joint authoring and direct movement;
`topic:"motion"` retains transmission couplings. The compact overview dispatches
these guides only when needed. The API has 70 tools, including read-only
`object_context` for selected components and their neighbouring joints.

Ground the support instance with `assembly.edit{id,fixed:true}`. A `revolute`
mate aligns the selected origins and +Z axes, leaving axial rotation. A `rigid`
mate aligns the full selected XYZ frames and locks all six coordinates. It does
not capture an arbitrary existing offset: use the actual producer attachment
frames. Discover native references with `reference_inspect` before attaching
parts. New rigid/revolute definitions require document version 29; documents
using earlier mate types keep their previous required format.

A `prismatic` mate aligns the complete XYZ orientation and the selected +Z
axes, leaving only translation along +Z. Unlike a concentric mate, axial spin
is locked. `slider` is an input alias saved as `prismatic`. This type and any
joint limits require semantic document version **31**, including their undo
journal; release identity alone does not change older documents' format.

`revolute` and `prismatic` accept persistent `limits:{min,max}`. Both bounds
accept finite numbers or parameter expressions; `limits:null` explicitly
removes them. The signed coordinate is **B relative to A**: revolute zero is
aligned reference +X, measured about A +Z in degrees; prismatic position is
the origin gap projected along A +Z in millimetres. The angular interval must
span less than 360 degrees. Its unique physical branch supports intervals such
as `[-45,45]`, `[0,120]` or `[120,240]` across the 180-degree cut. Multi-turn
angular state is unsupported. All evaluated bounds use
the existing finite +/-1e6 range. A pose or parameter edit outside the authored
interval rejects atomically; stops never change geometry or clamp a model edit.

For bicycle steering, inspect actual frame, fork, handlebar and wheel instance
IDs. Author a frame-to-fork revolute steering joint, a fork-to-handlebar rigid
attachment and a fork-to-wheel revolute axle. The head-tube and axle frames must
come from the authored parts; local `origin` is appropriate only when the
definitions deliberately use that attachment frame. A handlebar movement then
steers the fork and wheel together while retaining the wheel's current spin.

Preview a relative movement without saving:

```json
{"tool":"assembly_info","arguments":{"drive":{
  "driver":"Handlebar","mode":"rotate","delta":15
}}}
```

`driver` selects an instance name or ID. Optional `mate` selects a numeric joint
ID; without it, the nearest grounded joint must be unique. `delta` accepts a
number or parameter expression. `rotate` uses degrees; `translate` uses
millimetres and requires a prismatic or concentric joint. The returned `drive` block includes
`joint` (pivot, axis, moving nodes), `instances` with solved world matrices,
`diagnostics` and an `assembly.edit` pose recipe. The document, revision and undo
history remain unchanged. The outer assembly information still describes the
committed pose; use the `drive` block for the candidate pose.

The joint's `position`, `unit`, `limited`, `min`, `max` and `coordinate_sign`
describe the measured baseline coordinate and stops (bounds are present when
limited). A world-axis `delta` changes the coordinate by
`coordinate_sign * delta`; the sign is negative when A is the moving endpoint.
An out-of-range request returns `MANIPULATION_LIMIT_VIOLATION`, including a
full-turn request that would otherwise return to the same angular pose. The
returned geometric Jacobian rank and remaining DOFs exclude stop inequalities,
including a zero-width interval; they are not a full inequality-constrained
mobility calculation. Mate diagnostics report their measured coordinate and
the separate limit residual.

Commit the same relative movement at the inspected revision:

```json
{"tool":"doc_apply","arguments":{"expected_revision":12,"commands":[
  {"op":"assembly.drive","driver":"Handlebar","mode":"rotate","delta":15}
]}}
```

The revision above is illustrative; read the current value. `assembly.drive`
requires it. `doc_preview` can evaluate and cache this command; applying its
`candidate_id` reuses the evaluated pose and creates one undo entry. Saved poses
are ordinary parent-local assembly placements. Reload and undo/redo preserve
them. Select the wheel axle mate explicitly to spin only the wheel.

For a bridge joint, the solver moves the connected side as one exact rigid
motion, retaining internal passive coordinates. In **008D**, a grounded closed
loop or a transmission boundary instead uses constrained continuation from the
resolved pose: the selected signed coordinate is driven and all retained
geometric and active mechanical constraints are solved together. Passive links
can rotate and sliders can travel as required to close the loop. Grounded
carriers stay exactly at their committed transforms.

The verified domain includes planar four-bar linkages and a slider-crank with
revolute and prismatic joints, driven from either its crank or slider. A rigid
link can be one subassembly containing its visible body and explicitly authored
joint-pin instances; cross-parent mates retain that carrier's rigid pin spacing.
The pins' local `origin` names are authored attachment datums, not guessed mesh
points. A grounded carrier can contain a fixed chassis plus its declared pins.

Gear, chain and shaft ratios participate in the same drag preview and commit,
including reverse input from the driven rotational joint. Rack/pinion converts
angular radians to signed millimetres through its declared pitch radius and can
also be driven from its prismatic follower. Ratios act on the **relative movement
from the current pose**; a gear/chain phase offset is not reapplied on every drag.
A harmonic cam follower uses the measured grounded revolute input coordinate
plus its declared phase, so consecutive commits and reload preserve its phase.
This cam integration requires that explicit grounded input joint.

`drive.solver` distinguishes `rigid_side` and `constrained_continuation`.
`drive.continuation_steps` reports accepted small steps and
`drive.coupling_residuals` reports the measured final-step angular/linear
transmission equations. The geometric diagnostics continue to report every mate;
their rank and DOFs describe the resolved baseline, with stop inequalities
excluded. The continuation retains the previous pose as its deterministic local
branch and checks constraints and limits at every accepted step. A singular
branch, a passive rotation jump, an impossible coordinate, patterns, ambiguous
joints or an exhausted bounded numerical domain rejects the whole preview or
commit. Existing tolerances are unchanged. General spatial mechanisms, contact,
tooth/chain geometry, collision, force and dynamics are not qualified by these
planar and ideal transmission tests.

The synthetic [four-bar suspension](../examples/fourbar-suspension.arkops)
authors four exact component definitions, three moving rigid carriers, a fixed
chassis and four revolute joints. Replay its envelope in an empty document and
drag **Lower arm** or **Upper arm**. Its arm stops are -35 to 70 degrees and its
100 mm upright follows the closed linkage while retaining its pin spacing.
For an API preview use `driver:"Lower arm",mate:1,mode:"rotate",delta:20`.
Select the desired grounded arm joint explicitly when dragging the upright,
which is adjacent to two equally near drive choices. This is an editable concept
and an ideal kinematic example, without suspension force/contact or manufacturing
qualification. No new document schema is needed for continuation: persisted
placements, existing mates and their limits remain the authority.

In **007B**, Studio also starts joint movement by directly dragging a body in
normal body-selection mode. A click or small pointer motion remains selection;
movement begins only after the native drag threshold. Release commits one
revision-protected pose; Esc or lost capture restores the original pose. A direct
gesture returns to normal selection afterwards. Shift/Control selection gestures,
face/edge/sketch picking and middle-mouse orbit keep their existing behavior.

A drag can reach a jointed part through a transparent immovable cover, such as
the grounded barrel in the pneumatic example. An opaque surface still blocks
motion picking. Ordinary clicks, semantic face picks and context menus continue
to select the transparent cover itself. Rotation uses the chosen moving body's
hit coordinates, including when a differently placed cover is in front.

**Move component** remains available. If direct movement has multiple nearest
joint motions, the gesture leaves the document unchanged and asks you to use
this control to choose the motion explicitly; it never opens a modal selector
during a captured direct drag. The explicit mode stays armed until cancelled.
Both gestures use the same C++ preview and commit contracts. Studio's adaptive
grid follows view scale, with explicit units; the display grid does not rescale
geometry. Transparency uses the existing feature appearance contract,
described in [materials and rendering](RENDER_MATERIALS.md).

The synthetic [pneumatic cylinder](../examples/pneumatic-cylinder.arkops)
creates exact parametric barrel, rod and eye B-reps, a grounded barrel,
`prismatic` travel from zero to parameter `stroke` (80 mm), and a rigid eye
follower. Each definition intentionally shares the rod-base attachment datum
as local `origin`; the native eye transform places the eye at the rod tip while
preserving that declared attachment frame. Local `Z` therefore names the
authored cylinder travel axis without selecting a mesh or guessed face. On a
fresh document, replay its `doc_apply` envelope, select **Rod** or **Rod eye**
and move along the joint axis. For API inspection use:

```json
{"tool":"assembly_info","arguments":{"drive":{
  "driver":"Rod eye","mode":"translate","delta":"stroke"
}}}
```

This is a design and ideal kinematic example, without pneumatic pressure,
seal/contact, collision, catalogue or manufacturing qualification.

In the interface, select a part to use **Transparent/Opaque** or **Opacity**.
The change applies to every instance of that part definition. In **Model > Mate**,
choose `revolute`, `prismatic` or `rigid` from the type list and select the real attachment
references. Select the handlebar instance and choose **Move component**, then
drag it: release saves the pose; Escape cancels the preview. Middle-button orbit
remains available. A saved model without authored joints does not infer them
from visual proximity.
