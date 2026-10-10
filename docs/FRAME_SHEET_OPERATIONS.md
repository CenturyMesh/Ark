# Frame endpoints, weld beads and internal sheet bends

These are native, expression-bearing fabrication programs saved in document
version 25. Earlier frame/sheet programs retain their previous versions.
Use `doc_apply` with the current `expected_revision`. Any invalid operation
rejects the complete batch and leaves the prior document intact.

Run [frame-trim-weld.arkops](../examples/frame-trim-weld.arkops) or
[sheet-jog-bend.arkops](../examples/sheet-jog-bend.arkops) in an empty session.
Their dimensions are explicit examples, not process defaults. Inspect with
`frame_inspect`, `sheet_inspect`, `native_inspect` and `doc_info{detail:"design"}`.
STEP export preserves the exact member, bead and formed/flat sheet solids.

## Tube meetings

`frame.trim_extend{id,joint:{member,end:"start"|"end",against,max_extension}}`
supports two different straight round or rectangular members with intersecting
axes. It adjusts the selected endpoint to the host axis, then cuts the member
with the host's exact outer stock envelope. A tube's hollow internal region
does not become the cope tool. The host remains its own physical member.

Extension cannot exceed `max_extension`, which is a nonnegative millimetre
expression. A short brace can therefore reach the host without an invented
extension allowance. Trimmed stock must remain one valid positive-volume solid.
Parallel/skew axes, bent members, target ambiguity near a host end, missing
members and repeated operations on one endpoint reject explicitly. The original
round-on-round `cope` program remains supported within its earlier domain.

`frame.trim_plane{id,joint:{member,end,origin:[x,y,z],normal:[x,y,z]}}` cuts a
straight member across an explicit crossing plane and retains the other end.
Exterior or tangent planes cannot silently pass as a successful cut. Use
`frame.joint.remove{id,member,end}` before replacing an existing endpoint joint.

`frame_inspect` reports `authored_length_mm`, generated `developed_length_mm`,
removed exact volume and axis-length change. These are stock/centerline design
quantities. Endpoint frames remain design datums, rather than a claimed point
on every nonplanar cope boundary. Members also report ideal nominal section
second moments in mm4, extreme fibers and the local axis convention; these
cross-section properties are separate from physical mass inertia in g*mm2.
The fiber reported for bending about local y is the section's half-height in z;
the fiber for bending about local z is its half-width in y.

## End fits on existing authored tubes

`tube_end_fit` adds a linked exact solid operation to existing `tube` features,
their rigid placements, or earlier end fits. It preserves the original Tube
authoring program; it does not convert generic solids into a construction frame.
The Studio **Tube end fit** action creates the same operation:

```json
{"op":"feature.add","type":"native_solid","name":"Brace fitted",
 "inputs":["Brace placed","Host placed"],
 "native":{"op":"tube_end_fit","end":"start","mode":"envelope"},
 "p":{"max_extension":0}}
```

The result inherits its immediate source's declared material and authored
appearance when those fields are omitted. Explicit caller values override
each default independently; no manufacturing process or density is invented.

`mode:"envelope"` removes the exact filled outer stock of a straight authored
round or rectangular Tube host. The hollow region is filled in the cutting
tool, and the physical host remains independent. It trims existing stock only:
`max_extension` must be explicitly zero. A curved source is supported when the
selected end has a straight terminal segment. Removed material must belong
entirely to that terminal segment; interior crossings and cuts into bends reject.

`mode:"plane"` additionally requires `native.reference` naming a bound planar
face on the second input. Discover its semantic name with `reference_inspect`
or `native_inspect`; enumeration indices are not references. The operation
trims or extends the terminal straight segment, with `p.max_extension` as an
explicit nonnegative expression. The entire material cap must be supported by
the finite selected face. Parallel axes, insufficient extension, uncovered caps
and cuts outside the terminal segment reject. Extension of a previously trimmed
tube rejects because rebuilding its original stock would lose the earlier cut.

`mode:"plane_trim"` uses the same linked planar face, terminal ownership and
extension contract as `plane`, but permits a cap wider than the finite host
face. It trims the whole endpoint flush with the face's supporting plane.
This is explicit CAD construction intent: actual contact and zero material
overlap remain mandatory. `cap_coverage_checked` means the cap coverage was
measured; `covered_cap_area_mm2`, `uncovered_cap_area_mm2` and
`full_seat_checked` report whether the finite face supports the whole cap.
Partial coverage does not qualify sealing, weld continuity or fabrication.
Use `plane` when complete finite-face seating is a required design condition.

`native_inspect.report.end_fit` records source/result volumes, stock extension,
exact separation and overlap, terminal ownership, and planar cap coverage.
All modes also measure the retained area on the original selected cap;
an overwide tube may retain an exposed portion. Exact contact and zero overlap
prove coping, without proving complete seating, closure or a continuous weld.
Planar results expose `fit.start` or `fit.end`; deleted original cap references
remain deleted. Every result must be one valid positive solid, contact the
physical host, and have no positive-volume material overlap. The new operation
uses document version 30; application iteration codes do not change document
versions. This is end-fit evidence, without weld or manufacturing qualification.

## Explicit weld geometry

```json
{"op":"frame.weld.add","id":"Frame","weld":{
  "id":"Seam1", "member_a":"rail", "member_b":"brace",
  "path":[[90,-5,10],[90,5,10]], "normal":[-1,0,0],
  "leg_a":3, "leg_b":3, "material":"Declared steel"
}}
```

The current bead is a right-triangular straight fillet prism. `path` is exactly
two frame-local endpoints. The projected `normal` defines its first leg; the
seam direction crossed with that normal defines its second leg. Degenerate
directions, zero lengths, floating beads and bead material overlapping a member
reject. Both members must already exist and the exact bead must touch both.
Positive-volume overlap between beads also rejects to prevent counting the
same filler twice; beads may touch at their boundaries.

The bead remains a separate exact physical solid in the frame compound. This
does not turn overlapping members into one certified welded part. Inspection
reports its exact volume, declared legs, seam length and theoretical throat
`leg_a*leg_b/sqrt(leg_a^2+leg_b^2)`. Attachment means exact zero distance to both
selected members; it does not establish a continuous interface, fusion,
penetration, weld quality or strength. Those limitations remain in the report.

Use `frame.weld.edit{id,entity,values}` and `frame.weld.remove{id,entity}`.
Assign a named material or explicit `density_g_cm3` for bead mass. Missing bead
density keeps aggregate frame mass unchecked, even when member profiles have
density. Process, filler compatibility and weld design need separate evidence.

## Original-blank jog and sketch bend

The supported input is one unperforated rectangular `sheet.create` base, without
other flanges, hems or stamps. Generic exact solids and non-sheet geometry do
not establish this authoring route. Arbitrary shaped blanks, bend-crossing holes,
partial/diagonal/curved lines and multiple internal forming programs reject.

`sheet.jog.add{id,jog:{id?,station,height,angle,inner_radius,k_factor?}}` produces
two opposed full-width bends with a parallel distal panel. `height` is the
signed normal displacement from the original XZ blank; angle magnitude is at
most 90 degrees. `station` is the start of the first neutral-line bend band.
The program derives the web length from the declared offset and radii. An
offset too small for those bends or a blank too short for a positive distal
panel rejects. The generated panels are `<id>.web` and `<id>.distal`.

`sheet.sketch_bend.add{id,bend:{id?,sketch,entity,inner_radius,angle,k_factor?}}`
consumes one named straight line in a resolved XZ sketch. Its constant local X
coordinate specifies the bend-band start and its local Y span must cover the
entire blank width. The line is linked: parameter or sketch edits update the
bend. This is an actual bend of existing blank material, not an added flange.
The generated distal panel is `<id>.folded`.

Both programs retain the original authored base length in the document and
evaluate an exact panel/flange graph whose developed extent equals that blank.
The chosen K-factor remains explicit. K=0.5 retains the existing midpoint
geometric volume-conservation convention; other K values retain the existing
reported formed/flat geometric difference, without claiming calibrated material
conservation. Existing exact interference and independent refold checks apply.

`sheet.jog.edit` and `sheet.sketch_bend.edit` take `values`; their `remove`
operations restore the original rectangular blank. `sheet_inspect.forming_ops`
reports the original length, station and declared offset. These operations do
not certify bending strain, tooling, springback or manufacturing qualification.

If a generic Boolean reports that it cannot produce a positive solid, inspect
its exact inputs, overlap and operation first. A disconnected union or a cut
that removes the entire body needs a corrected design operation. Tube joints
and weld beads should use the explicit frame program rather than weakening
the native solid validity contract.
