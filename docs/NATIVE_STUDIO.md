# Native modeling in Studio

Open **Commands** or press **Ctrl+K**. Studio applies the forms to the active
`.ark` document through the same atomic history used by the agent connection.
Undo, redo, save, and live MCP see the resulting features. The viewport shows
tessellated geometry; native STEP export uses the validated OCCT B-rep.

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

**Fillet / chamfer named edges** uses bound semantic edge names shown by
`reference_inspect` or the selected native solid's form hint. If an earlier
operation deletes or splits an edge, choose a currently bound reference.
**Rigid transform** places one native input by XYZ translation and XYZ Euler
angles. Select a native solid for hole, edge treatment, datum, and Boolean
shortcuts in the inspector. Native solid STEP export uses its exact B-rep.

**Native surfaces** provides Loft sketches, Fill closed sketch, NURBS surface,
and Thicken selected surface. Loft lists two or more saved sketch names in
section order, separated by semicolons. Fill uses a saved closed sketch.
The NURBS editor accepts poles, optional weights, and two degrees. Thicken
acts on a selected native loft, fill, or NURBS surface.

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
