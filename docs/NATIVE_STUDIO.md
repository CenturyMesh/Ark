# Native modeling in Studio

Use **Search commands** or press **Ctrl+K**. Studio applies the forms to the active
`.ark` document through the same atomic history used by the agent connection.
Undo, redo, save, and live MCP see the resulting features. The viewport shows
tessellated geometry; native STEP export uses the validated OCCT B-rep.

The top tabs are **Sheet Metal**, **Model**, **Sketch**, **Inspect**, **Tools**,
and **Manage**. Sheet Metal groups its actions as Create, Modify, Pattern,
and Output. Model contains exact construction and assembly shortcuts;
**Model tools** retains native surfaces, frames, suspension, electronics,
routing, and mesh tools. Tools contains engineering and agent connections.
Manage contains document, history, parameter, import, and account actions.
The command search is a native text field: type a term and press Enter to
open matching commands. Ctrl+K opens the complete grouped command palette.

The left sidebar has **Model**, **Layers**, and **Properties** tabs. Model
shows a compact nested document tree with feature operations, sketches,
reference geometry, parameters, and manufacturing views. Its folders and
sheet feature chevrons expand and collapse. Layers controls per-instance
visibility and Show all without changing the document revision. Properties
contains the scrolling inspector. The Assistant opens a separate collapsible
conversation dock. The camera-oriented Cartesian triad remains at the lower
right with labeled red X, green Y, and blue Z ends.

Right-click a body, sketch, datum, history feature, or sheet operation for
its native context menu. Model, Inspect, and View groups use vector icons,
concise labels, and native keyboard navigation. Sheet nodes reveal their
flange, return-hem, circular-emboss, and opening entities. Double-click a row
to edit that stable entity; its context menu also removes it atomically.

The viewport defaults to a dark graphite background and shaded exact display
meshes. **Shaded** at its upper right selects shaded, edges, wireframe,
orthographic projection, graphics settings, and named views. Its optional
view cube can be shown through this menu. The bottom status bar controls the
grid, optional navigation toolbar, and full screen. Named face views are
orthographic; free orbit and Isometric remain perspective. Keys 1 to 7 select
front, back, top, bottom, right, left, and isometric. View choices and display
preferences do not change STEP or the agent protocol.

## Mechanical relations and motion

Model's Assembly group contains **Mate**, **Relation**, and **Motion**.
Mate retains the geometric positioning constraints. Relation authors a chain,
gear, shaft, rack-and-pinion, or harmonic cam relation between instance IDs
and exact semantic axes. Chain and gear forms accept tooth-count expressions;
rack forms accept pitch radius and direction; cam forms accept lift and phase.
Motion chooses a driver instance, speed expression in rpm, and elapsed time.
The native program is built once per document revision and evaluated per frame.
Its display transforms preserve the authored geometry and document revision.

The motion panel shows tooth counts, ratio, cadence, rear-wheel rpm, and time.
**Play / Pause** advances or holds the read-only pose; **Reset** restores the
formed baseline. For the bicycle example, **Teeth** edits `front_teeth`,
`rear_teeth`, and `cadence_rpm` together, recomputing native sprocket geometry
and the motion ratio. Undo restores the previous parameter program. Cadence
can also be edited through Motion, and individual expressions through the tree.
This is prescribed kinematics; tooth contact, chain collision, cam contact,
and dynamics are not qualified by the playback.

For a reproducible pose, use `--motion-driver "Front crank" --motion-rpm 60
--motion-time 0.125`; `--motion-play` starts live playback. The dedicated
`--self-test-motion-ui` loads `examples/bicycle-transmission.ark`, checks the
48/16 ratio at 60 rpm (rear wheel 180 rpm, 135 degrees at 0.125 s), changes
the rear sprocket to 24 teeth through the native form (120 rpm, 90 degrees),
verifies undo, and confirms that playback does not change the revision.

## Sketches and construction datums

Create a rectangle, circle, or interpolating spline from **Sketch & planes**.
Select a sketch in the left explorer to show it in the viewport and inspector.
**Edit entity** changes coordinates or a spline anchor using numbers or named
parameter expressions. **Add constraint** accepts `fixedX`, `fixedY`, `fixed`,
`distance`, `horizontal`, `vertical`, `parallel`, `perpendicular`, `radius`,
`coincident`, and `angle`. A constraint uses entity names or 1-based entity
numbers; point slots are 0 for the start and 1 for the end. Select a constraint
row in the inspector to highlight its referenced geometry, then edit or remove
it. On a shorter window, **Edit constraint** and **Remove constraint** ask for
its 1-based number. The raw sketch JSON editor remains under **Commands** for
advanced changes.

The inspector reports free dimensions and local Jacobian rank. Constraint
rows show satisfaction, local redundancy, or a conflict candidate. These are
numerical findings at the current solved coordinates; they do not prove global
uniqueness or identify a minimal conflict set. A conflicting form edit lists
the violated constraint IDs or numbers and residuals before any commit; the
saved sketch stays unchanged. The `sketch_diagnose` tool can evaluate a conflicting scratch
sketch and report per-constraint residuals without committing it.

Use **Create / edit plane** for a parent datum and expression-driven XYZ offsets
and rotations. **Attach plane to feature** supports a planar face, the
midplane between two parallel planar faces, an edge or axis normal at a curve
fraction, or a station along an authored tube. Enter semantic references from
`reference_inspect`; these references belong to native topology rather than
viewport triangle numbers. Choose **plane**, **axis**, or **frame** as the
display kind. The **Toggle datum frames** command or the inspector's **Show
datums** button shows named references. Plane rectangles, local Z axes, and
XYZ frames are drawn according to their chosen kind. Select a datum in the
left explorer to center the view on its origin. `plane_inspect` reports its
resolved frame and named axes.

## Exact solids and surfaces

The **Exact solids** menu creates native boxes, cylinders, spheres, cones, and
tori with expression-driven dimensions. **Extrude saved sketch** consumes a
closed sketch profile. **Union / cut / common** takes two feature IDs in history
order. **Hole / slot / profile pocket** takes a base feature, a named datum,
local XY position, offset along the datum normal, and cut depth. Its cutter
travels in local positive Z; set the offset so the cutter starts before the
material and crosses it. Profile pockets use a saved closed sketch.

The inspector filters **Body**, **Face**, **Edge**, and **Curve**. Click a face
or an edge on the exact solid; the highlight uses its semantic name, not a
triangle number. Click a visible sketch curve, or the sketch in the explorer,
to collect that sketch for a loft or a sweep. With **Face** active, a datum in
the explorer is a mirror plane. At most 64 names are kept. Feature editors for
fillet, chamfer, draft, mirror, pattern, Boolean, hole, extrude, loft, sweep
and the other solid and surface tools stay open while the view is clicked.
Each click adds the semantic reference; Apply writes the same feature command
as a typed name. **Fillet / chamfer** uses those clicked edges, or the selection
`sharp`, `convex`, `concave`, or `all` with a minimum dihedral angle in degrees.
At most 64 edges are taken. A loft
publishes `edge.start.<entity>`, `edge.end.<entity>`, and `rail.<index>` in
addition to its face names. **Draft faces** takes an angle in degrees, a pull
direction `X`, `Y`, or `Z`, and a neutral coordinate plane. It accepts planar,
cylindrical, and conical faces; a freeform face is rejected. **Linear /
circular pattern** and **Mirror solid** keep separate exact solids unless
**Fuse** is on. Mirror is also under **Modify**. Its plane is `XY`, `XZ`,
`YZ`, a named datum, or one selected planar face. A later union, cut, or
common accepts that compound. If an earlier operation deletes or splits an
edge, choose a currently bound reference.
**Rigid transform** places one native input by XYZ translation and XYZ Euler
angles. Select a native solid for hole, edge treatment, datum, and Boolean
shortcuts in the inspector. Native solid STEP export uses its exact B-rep.

**Native surfaces** provides Loft sketches, Fill closed sketch, NURBS surface,
Sweep sketches, Extrude sketch surface, Revolve sketch surface, Offset selected
surface, Sew surfaces, and Thicken selected surface. Loft lists two or more
saved sketch names in section order, separated by semicolons. A one-point
sketch is a tip. Smooth and compatible are optional toggles. Fill uses a saved
closed sketch. The NURBS editor accepts poles, optional weights, and two
degrees. Sweep asks for a section sketch, a path sketch, and a solid toggle.
Extrude and revolve use one saved sketch; the revolve axis is `X`, `Y`, or `Z`
and is stored as `native.axis`, not as an expression. Offset and thicken act
on the selected open native surface. Thicken accepts a loft, fill, NURBS,
sweep, extrusion, revolution, or offset. Sew lists 2 to 16 feature IDs
separated by semicolons.

Select a native surface, mesh feature, or analytic solid and choose **Edit feature**, or double-click its row in the explorer. The form opens on the saved definition and applies `feature.edit`, so that feature and its dependents recompute. **Edit sketch** opens a driving sketch; a loft asks which section. Double-click a sketch row to edit its anchors. The explorer scrolls through features, sketches, and datums. The first six named parameters stay on the explorer and recompute every sketch or feature that uses them. Imported source geometry and imported meshes stay baked snapshots. Sew keeps its input list; the solid choice is editable.

**Mesh** provides Weld, Smooth, Decimate, Offset, Fill planar holes, and
Import STL/OBJ. These commands add a mesh feature and do not attach a `native`
object. When the selection is an exact B-rep, the form includes **Sample exact
B-rep** and requires it to stay on; the result is mesh authority. Import copies
the STL or OBJ into the document. An optional weld distance merges vertices.
The file is not linked.

## Authored sheet metal

Choose **Sheet metal → Create sheet base** to enter its name, length, width,
thickness, and declared K factor. Select the sheet, then use its inspector or
the **Sheet metal** menu to add a flange. Each flange has a stable panel ID,
an earlier parent panel, one parent edge (`u_min`, `u_max`, `v_min`, or `v_max`),
length, optional width and offset along the edge, inner radius, angle, and
optional K factor. The form accepts `parent_length`, `parent_width`,
`edge_length`, `thickness`, and named parameters in dimension expressions.
The menu also edits the base, edits or removes flanges, and adds, edits, or
removes circular and polygonal openings. Openings use panel local XY
coordinates and may use `panel_length`, `panel_width`, `thickness`, and named
parameters. Polygon points are entered as `x,y; x,y; ...`. Removing a flange
with descendants or openings requires the **Remove dependent
flanges/openings** choice; undo restores the previous program.

The earlier **Create L bend** form remains available. Editing one of those
authored sheets promotes its two legs to stable base and flange identities.
An imported source sheet stays read-only.

**Hem** creates an open round 180-degree edge return with a positive inner
radius, return length, direction +1 or -1, and optional width, offset, and
K factor. A hem is terminal and cannot own child flanges. **Stamp** creates
the supported circular emboss with a sharp conical wall, planar plateau,
and exact constant normal thickness; a negative depth makes a recess.
A stamped sheet's Flat endpoint is a **nominal flat reference**, not a
qualified production blank. Intermediate forming strain is not modeled,
so Unbend / Play is unavailable for stamped sheets and flat STEP/DXF export
is rejected by the geometry contract.

An exact rigid transform of a sheet retains its original sheet identity.
Selecting the installed part exposes sheet tools that edit that source
definition and recompute the placement. **Flat** lays out original previews
in common part coordinates; **Formed** restores the exact transformed bodies
and assembly positions without applying a placement twice. These display
changes do not alter the document revision or authored frames.

**Flat** and **Formed** show the exact validated endpoint meshes. An authored
sheet also has **Play bends** and a draggable slider. Intermediate frames use
a display-only neutral-line animation built from the saved panel program;
scrubbing updates display mesh, selection, and measurements without changing
the document revision or exported geometry. Native STEP export still asks
for **flat** or **formed** and defaults to **flat**. The declared K factor is a
flat-development design input, with no inferred material calibration.

**Import sheet STEP** asks for an explicit K factor and opens a Unicode file
picker. It loads a supported source sheet as read-only geometry in a new
document. Optional zero-based body index and root panel choice resolve
ambiguous source bodies. The inspector shows projection and refold evidence.
When the original STEP remains present, source export checks its recorded
digest; a changed source must be imported again.

For a local UI check, run `ark-studio.exe --shot <temporary-png-path>
--self-test-sketch-ui` or `ark-studio.exe --shot <temporary-png-path>
--self-test-native-ui`. These opt-in checks need a Windows OpenGL 3.3 context.
The native check also writes a flat STEP beside the chosen PNG. The current
build must contain the Studio UI changes being tested.

`--self-test-context-ui` also checks native menu ownership and labels, exact
sheet-transform restoration, hem/emboss forms, responsive ribbon bounds,
Properties scrolling, and Assistant toggling at 1280x800 and 1600x1000. It
writes context-menu and workspace/dock PNG evidence beside the requested shot.
