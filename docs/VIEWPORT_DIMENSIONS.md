# Exact extrusion gestures and authored solid dimensions

This extends the local authoring scope of **B.01** and **B.02**. Both milestones
remain **Parcial**; the complete navigation and feature-history acceptance criteria
remain in the roadmap.

## Extrude a closed sketch by dragging

Select **Model > Drag extrude** (also available under Sketch > Build), choose a
saved closed sketch, and drag in the view along the cyan normal guide. The exact
native extrusion is evaluated in an isolated candidate. Releasing saves a valid
candidate as a `native_solid` extrusion. The profile and sketch plane remain its
source. **Tab** accepts a depth expression; **Apply** saves it, and **Esc** cancels.
At compact window widths, the Model ribbon keeps the main Create, Direct and
Combine actions readable; **Model tools** retains the assembly, frame and other
advanced commands.
The current exact sketch-extrusion contract requires a positive depth. A reversed,
zero, open or otherwise invalid extrusion is rejected with the source retained.

For an existing exact sketch extrusion, choose **Push / pull** and drag its
`cap.end` face. The gesture changes the authored depth expression while preserving
the starting cap and source profile. **Tab** enters a signed increment expression.
This operation supports the original extrusion feature, including a named datum
plane; downstream Boolean or transformed solids must be edited through their
source history. Placed occurrences require editing their part definition.

## Double-click a solid

Double-click the visible solid in Body mode to expose its authored dimensions.
Each row shows the evaluated value in the selected display units and its stored
expression. Click a row to edit that expression. Named parameter bindings remain
editable; the row displays source parameters, rather than inferred tessellation
bounds. Geometry validation and publication are atomic.

**All source values** opens the full source editor. **Edit driving sketch** opens
the profile where its line, radius, arc or rectangle dimensions can be changed.
A feature without supported scalar dimensions offers **Edit feature**. The
viewport shows as many rows as fit; the full editor keeps the remaining values
available. **Esc** or **Close** dismisses the overlay. Editing a placed occurrence
changes its shared part definition and consequently every occurrence.

## Agent dimensions use the same source

Inspect `doc_info{detail:"design"}` to obtain the current revision, feature IDs
and parameters. To add a named depth and bind an existing extrusion, apply both
commands in one transaction at that observed revision:

```json
{
  "expected_revision": 12,
  "commands": [
    {"op": "param.set", "name": "depth_mm", "expr": "25"},
    {"op": "feature.edit", "id": 7, "p": {"depth": "depth_mm"}}
  ]
}
```

The sketch onboarding also returns `dimension_create_example` and
`dimension_edit_example` with the live observed `expected_revision`. For example,
a new driving radius can be authored as one parameter and one linked sketch:

```json
{
  "expected_revision": 12,
  "commands": [
    {"op": "param.set", "name": "profile_radius_mm", "expr": "7"},
    {"op": "sketch.set", "name": "Dimensioned circle", "sketch": {
      "plane": "XY",
      "entities": [{"kind": "circle", "name": "ring", "dof": [0, 0, "profile_radius_mm"]}],
      "constraints": []
    }}
  ]
}
```

Refresh the revision before altering that radius with
`doc_apply{expected_revision:13,commands:[{op:"param.set",name:"profile_radius_mm",expr:"9"}]}`.
The example creates a new sketch; retain the existing profile's full entity and
constraint source when editing a saved sketch. Numeric solver variables can
instead use a radius constraint such as `{type:"radius",a:0,val:"profile_radius_mm"}`.

An agent can subsequently change `depth_mm` with `param.set`, or edit a feature
parameter with `feature.edit`. Use actual IDs and revision from the inspection.
For profile dimensions, edit the authored sketch or its named driving parameter;
the solid follows the source on recomputation. Stale revisions and invalid
geometry reject without silent rebase or partial publication.

## Verification scope

The dedicated `direct-dimensions` test measures exact native extrusion volume on
an XZ source plane, a retained cap displacement expression, subsequent agent
radius/depth changes, negative and stale write rejection, and persistence.
`studio-direct-authoring-ui` exercises the rendered tool, drag preview and release,
Tab expression entry, exact cap editing, viewport double-click, clickable source
rows, invalid edits and a stale displayed-row receipt. It also retains the
existing primitive and indexed mesh gesture checks.

These are local geometry and Studio checks. They do not qualify fabrication,
external integrations, distribution or every feature type.

Local check details: [verification receipt](verification-direct-dimensions.json).

![Authored solid dimensions](images/020B/solid-dimensions.png)

![Exact sketch extrusion preview](images/020B/exact-extrude-preview.png)
