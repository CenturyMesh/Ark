# Joint-aware assembly manipulation

Milestones **B.01**, **B.04** and **D.04**: inspect interiors and manipulate an
editable assembly through persistent, revision-checked contracts. This is an
ideal rigid kinematic model; collision, forces and manufacturing qualification
require their own evidence.

`agent_onboard{topic:"assembly"}` describes joint authoring and direct movement;
`topic:"motion"` retains transmission couplings. The compact overview dispatches
these guides only when needed. The API has 67 tools, including read-only
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

The solver moves the connected side of the selected joint as a rigid motion,
retaining internal passive coordinates and rechecking all retained mates.
Ambiguous joints, closed loops, grounded moving sides, unsupported coupling
boundaries and unsatisfied constraints reject atomically. There is no independent
visual pose accepted as model authority.

Studio's Move control uses the same C++ preview and commit contracts. Its
adaptive grid follows view scale, with explicit units; the display grid does not
rescale geometry. Transparency uses the existing feature appearance contract,
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
