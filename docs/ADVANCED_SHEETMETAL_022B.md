# Advanced sheet authoring — 022B

Milestones **B.01**, **B.02** and **C.01** remain **Parcial**. This extension
adds editable exact geometry in explicitly bounded domains. It does not certify
tooling, welds, strain, bend order, collision during bending or fabrication.

## Source and publication

Authored sheet features retain recipes in `native`: `miters`,
`swept_flanges`, `corners`, `rips`, `tabs`, and optional `bend_table`.
The worker expands contour recipes in a disposable evaluation request; the
saved source remains the common profile and named edge selection. Changing
dimensions, profile lengths, radius, gap or a named expression regenerates both
formed and flat exact B-reps. There is no stored mesh standing in for a recipe.

New recipes require document format **37**. Earlier files retain their prior
minimum format. A file tagged with an older format must reject these fields;
the application identity does not independently advance `.ark` format.
Source reads, undo and redo preserve the authored recipes. Private user drawings
are excluded from this implementation and its tests.

All edits use `doc_apply` with the current `expected_revision`. The entire draft
evaluates before publication. Missing IDs, invalid dimensions, overlapping
material, disconnected solids, unavailable bend-table rows and failed refold
checks reject the mutation and preserve the source and revision. No tolerance
increase, implicit solid deletion or repair retry is used.

Recipes use stable source IDs. Generated contour panels use `<id>.<segment>`;
miter panels use `<id>.<edge>.<segment>`. These names can own later openings and
edge treatments. Removing a source with surviving dependants rejects until the
dependants are explicitly removed in the same atomic batch.

## Miter flange

`sheet.miter.add` carries `miter`; `.edit` carries `entity` and `values`;
`.remove` carries `entity`. The native list is `miters`.

```json
{"expected_revision":12,"commands":[{"op":"sheet.miter.add","id":1,
 "miter":{"id":"rim","parent":"base","edges":["u_max","v_max"],
 "gap":1,"profile":[
   {"length":35,"angle":90,"inner_radius":3},
   {"length":12,"angle":-90,"inner_radius":3}]}}]}
```

The supported common profile is a wall and outward lip, with opposite 90-degree
bends, on 2–4 edges of the original rectangular base. At least one adjacent
edge pair is required. Both positive and negative profiles are supported.
Each participating wall attachment is shortened at a selected corner by
`wall_inner_radius + thickness + gap`. The terminal coplanar lips receive exact
planar bisector trims and extensions. Their boundary planes remain normal to
the sheet skins, preserving full normal thickness. The symmetric normal gap
is the authored `gap`; no material crosses that seam.

This is a real shared contour operation with geometric corner seams. It is
limited to this wall/lip profile. Arbitrary non-perpendicular miter profiles,
curved perimeter paths, automatic three-bend transition relief and overlap seams
remain unsupported. Formed and developed-layout interference checks also
reject otherwise valid profiles whose chosen lengths cause collisions.

## Straight swept flange / contour

`sheet.swept_flange.add/edit/remove` uses `swept_flange`,
`swept_flanges` and the same edit/remove convention.

```json
{"id":"channel","parent":"base","edge":"u_max",
 "width":"edge_length-10","offset":5,"path":"straight_edge",
 "profile":[{"length":30,"angle":90,"inner_radius":3},
            {"length":12,"angle":-60,"inner_radius":4}]}
```

The profile contains 1–8 ordered straight spans and cylindrical bends. It is
swept at constant section along a named straight parent edge. Segment lengths
are tangent spans. The first segment can use `edge_length`, `parent_length`,
`parent_width`, `thickness` and document parameters; subsequent segment
expressions see their actual predecessor. `k_factor` may be authored per
segment. Negative bends are supported. Curved or stretched sweep paths reject
explicitly; the UI labels the domain as straight swept flange / contour.

## Corner and edge operations

`sheet.corner.add/edit/remove` uses `corner` and `corners`. Corner indices follow
local `(u,v)`: 0=`(0,0)`, 1=`(length,0)`, 2=`(length,width)`,
3=`(0,width)`.

| Kind | Authored fields | Actual geometry |
| --- | --- | --- |
| `relief` or `trim`, `shape:rectangular` | `panel`, `corner`, `width`, `height` | Through-thickness rectangular corner cut. |
| `relief` or `trim`, `shape:round` | `panel`, `corner`, `radius` | Quarter-circle through-thickness corner relief. |
| `break` | `panel`, `corner`, `distance` | Equal-distance planar corner chamfer. |
| `closure` | `panel`, `target`, `side:start/end`, `gap`, `from` | Terminal flange side extension toward a perpendicular target's nearest skin plus the matching terminal wall extension, beginning at source local `u=from`; positive gap stays open. |

Closure targets must have their sheet normal aligned with the extending panel's
span direction, and the wall tangents must be parallel. Both panels must be terminal, and each extension must remain
connected and avoid other exact sheet pieces. Bend regions remain open. This
adds sheet material to both matching sides. The gap is measured again between
the final finite exact panels. It does not add a weld bead or qualify a butt/overlap weld.
Changing a recipe's kind should replace its fields through atomic remove/add
with the same ID, as the human editor does.

`sheet.rip.add/edit/remove` uses `rip` and `rips`, with
`panel`, `edge`, `station`, `width`, `depth`. It cuts a rectangular opening from
a straight edge, strictly inside the edge span and with partial inward depth.
This bounded edge-open rip/notch does not rip an imported closed shell and does
not cross a cylindrical bend band. A cut that splits a panel rejects.

## Tab and paired slot

`sheet.tab.add/edit/remove` uses `tab` and `tabs`:

```json
{"id":"joint","panel":"back","edge":"u_max","station":25,
 "width":10,"length":6,"slot_panel":"base","slot_x":50,
 "slot_y":40,"clearance":0.3}
```

The recipe adds a rectangular tab to an unattached straight panel edge and
cuts a real rectangular slot in a different panel. The slot's local `u` size is
`tab_width + 2*clearance`; its local `v` size is
`thickness + 2*clearance`. Slot centers are explicit local coordinates.
Clearance must be positive; tabs and slots must avoid existing cuts and preserve
one connected sheet. These are authored paired features. The current receipt
sets `assembly_fit_verified:false`: it does not reposition parts, verify mating
at an assembled pose, or infer fit, laser kerf or fabrication capability.

## Bend allowance table

`sheet.bend_table.set` carries `table`; `.remove` removes the table. It affects
the developed geometry and independent refold construction, rather than
attaching unused metadata.

```json
{"op":"sheet.bend_table.set","id":1,"table":{"interpolation":"exact",
 "entries":[{"thickness":2,"inner_radius":3,"angle":90,
             "bend_allowance":5.969026041820607}]}}
```

Keys are thickness, inner radius and unsigned bend angle. Each bend must match
exactly one row (fixed numerical comparison `1e-9`); duplicate keys, missing
rows, interpolation and extrapolation reject. Effective
`K=(allowance/angle_radians-inner_radius)/thickness` must lie in `[0,1]`.
A conflicting explicit segment K factor rejects. The sheet default remains
authored, while each bend reports its effective K and
`bend_allowance_source:authored_exact_bend_table`. This records an explicit
allowance input; material/shop calibration requires separate evidence.

## Relation to the requested sheet toolbox

| Requested tool | 022B position |
| --- | --- |
| Convert to sheet | Existing bounded read-only source STEP import with explicit K; generic conversion remains open. |
| Edge flange | Existing authored finite cylindrical bends and signed panel tree. |
| Miter flange | Added common two-bend wall/lip contour and exact coplanar seam domain. |
| Swept flange | Added straight-path constant contour; general curved/conical sweep remains open. |
| Hem, jog, sketched bend | Existing open round hem and original-blank jog/sketch-bend domains. |
| Cross break, gusset | Open; no shallow strain model or invented manufacturing claim. |
| Closed corner | Added bounded positive-gap terminal side extension. |
| Welded corner | Open; closing a gap does not author weld material. |
| Break corner, corner relief, corner trim | Added exact chamfer/quarter-circle/rectangular panel treatments. |
| Bend notch | Partial edge-open notch through `rip`; trimming curved bend bands remains open. |
| Unfold, fold | Existing exact formed/flat B-reps and read-only bend preview; these do not edit the stored source or certify a trajectory. |
| Lofted bends | Open. A generic solid loft is not a qualified developed sheet transition. |
| Bends | Existing and contour-generated cylindrical bends with authored development inputs. |
| Rip | Added partial straight-edge opening; closed-shell seam ripping remains open. |
| Tab and slot | Added real paired source geometry and explicit clearance; assembled fit remains open. |
| Normal cut | Existing panel-owned through-thickness openings plus these planar edge treatments; cut across curved bends remains open. |
| Stamp, forming tool | Existing bounded circular emboss; general die/forming-tool libraries and strain development remain open. |
| Bend table | Added exact keyed allowance authority; manufacturing calibration and table interchange remain open. |

## Validation evidence and limits

`tests/advanced_sheetmetal_tests.cpp` exercises two/four-edge miters in both
directions, real shaped lips, straight contour bends, allowance-derived K,
quarter-circle/rectangle/chamfer/notch material removal, gap closure and tab/slot
geometry. Every successful case checks valid connected exact formed/flat
solids, independently authored area/volume integrals and whole-solid refold
symmetric difference under the existing fixed tolerances. Rejections include
zero gaps/clearance, unsupported profiles/paths, missing allowance keys and
splitting rips. Source tests check format 37, reevaluation after reload, stale
revision rejection, failed geometry rollback and undo preservation.

Local OCCT 8.0.1 runs passed `advanced-sheetmetal` (8.40 s), `sheet-program`
(3.28 s), and `sheet-completion` (5.43 s). The advanced run includes persistence,
revision guards, rollback, old-format rejection, finite seam-gap measurements
and compact inspection receipts. Visual evidence is recorded separately by the
release validation. Deployment, external interoperability and manufacturing
qualification are outside this local evidence.

## Primary design references

The published tools informed the semantics and explicit boundaries, without
claiming API or feature parity. SOLIDWORKS describes multi-edge common profiles
for [miter flanges](https://help.solidworks.com/2025/english/solidworks/sldworks/c_Miter_Flanges.htm),
an open profile and path for
[swept flanges](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_creating_swept_flanges.htm?id=25.1.10.0),
and added corner material with gap/overlap controls for
[closed corners](https://help.solidworks.com/2025/english/solidworks/sldworks/c_closed_corners.htm).
Its [gauge/bend tables](https://help.solidworks.com/2025/english/SolidWorks/sldworks/c_sheet_metal_gauge_bend_table.htm?id=25.3.12)
associate thickness, radius and allowance inputs.

Autodesk Inventor documents an edge collection for a common
[contour flange](https://help.autodesk.com/cloudhelp/2025/ENU/Inventor-API/files/ContourFlangeDefinition_Edges.htm)
and distinct sheet, bend, corner and development inputs in its
[sheet/unfold rules](https://help.autodesk.com/cloudhelp/2025/ENU/Inventor-Help/files/GUID-4141CA32-0DCE-4806-AE73-EF9CD97B0206.htm).
Inventor's table rules can use backup K factors; ARK's bounded exact table
instead rejects absent keys. Its
[lofted flange documentation](https://help.autodesk.com/cloudhelp/2014/ENU/Inventor/files/GUID-B4EC6491-457D-41AF-BE28-5C87D38E1BDB.htm)
distinguishes formed and press-brake outputs, reinforcing why a generic loft
does not establish ARK sheet development or fabrication qualification.
