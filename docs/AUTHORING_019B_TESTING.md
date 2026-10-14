# Test 019B authoring

The user authorized completion and merge of PR #6 on 13 October 2026 after the
required checks pass. This authorization does not establish a human design
quality benchmark or manufacturing qualification. Prepare an isolated native
development executable from the repository root; keep test changes in copies
of existing drawings:

```powershell
./scripts/build.ps1 -Test
./scripts/prepare-test-preview.ps1 -BuildDirectory out/native-release -Name 019B-local
& './out/test-preview/019B-local/ark-studio.exe'
```

The synthetic example contains a dynamic Skeleton pattern, a helix and a loft:

```powershell
& './out/test-preview/019B-local/ark-studio.exe' --script './examples/authoring-019B.arkops'
```

| Workflow | Check |
| --- | --- |
| Image reference | Attach or drop a PNG/JPEG in Assistant. Inspect its thumbnail. Switch **Inspire design / Image to CAD**; enter any subset of Width/Height/Depth in mm, or leave all blank. Send a request through an image-capable connector. Check the visible intent and supplied estimates. Remove/cancel must send nothing. |
| Solid operations | Select operands and use **Fuse bodies**, **Carve body**, or **Keep overlap**. Native failures must remain explicit. |
| Source face push | Create a native box. Activate **Push / pull source face**, drag its positive height/width/depth face, and use Tab to enter a value/expression. Inspect preview and dynamic dimension, then Apply. Escape cancels. Cylinders/cones support their positive planar height face. Other faces use **Source dimensions**. |
| Mesh editing | Import an indexed mesh, or explicitly use **Sample and weld mesh** on a native body. Try **Extrude mesh patch**, **Move mesh vertex**, **Move mesh edge** and Tab expressions. Inspect the source-linked `mesh_edit` history and closure. Save/reload, Undo/Redo, and change an upstream coordinate without changing its connectivity. |
| Exact extrusion gestures | Choose **Drag extrude** with a saved closed sketch; drag a native preview and release to commit, or Tab a positive depth expression. Cancel before release and confirm no revision change. Push / pull its exact `cap.end`, preserving source sketch and start cap. |
| Solid dimensions | Double-click a visible solid, click an authored dimension and use a named expression. Change its source parameter through agent tools, verify native recomputation, and check invalid/stale edits retain the document. Inspect the overlay at supported window sizes. |
| Direct face and edge selection | Use the persistent **Body / Face / Edge / Curve** selector. Hover a visible cylinder/loft face or edge: the cyan target must match the next click. A plain click replaces the selection; clicking it again retains it. Ctrl or Shift adds/removes individual references. Switching selection modes must exit sketch editing, direct authoring and latched Pan/Zoom. Middle mouse still orbits. |
| Mesh selection | In Face mode, click an indexed cube panel: select its connected coplanar source triangles with a visible boundary. In Edge mode, hover/click its source edges. These return distinct `mesh_face` / `mesh_edge` visual selections with local indices and a topology guard; native geometry tools must not receive invented B-rep names. |
| Body glow and occlusion | Select a body directly or from the tree and inspect its halo. Orbit, then place another opaque body in front: hidden faces/edges and the rear halo must remain occluded. **Select adjacent tangent faces** expands a bounded native neighboring patch. |
| Sketch dimensions | Enter sketch editing, select a line/circle/arc or saved rectangle and click its displayed dimension (or Tab). Bind a named size, change it, and confirm recomputation and retained anchors/closure. Typed recipes expose their source dimensions. |
| Surface joining | Use **Glue surfaces** with touching exact surface inputs. Inspect free boundaries; disconnected/gapped inputs must reject in the connected workflow. Closed solid conversion requires a valid closed shell. |
| Helix and loft | Use **Helix curve** with editable radius/pitch/turns/axis. Consume the path in a sweep with a suitably positioned section. Select a loft and open **Loft section and guide controls**; edit seams, reversal and measured guide limits. |
| Skeleton editor | Validate the example source, Preview and inspect the candidate capture, Apply, then change `span`/count through named inputs. Cancel a source edit. Invalid or stale application must preserve the drawing. |
| Collision | Open **Inspect → Collision check**. Choose definitions or concrete assembly occurrences/copies. Inspect exact distance/overlap or labelled triangle contact/containment evidence. This is static geometry feedback; response dynamics are not simulated. |

Meshes retain triangle authority; their edits do not become exact production
B-reps. Boundary contacts do not imply a measured mesh intersection volume.
Image prompts guide editable reconstruction but do not supply automatic pixel
vectorization or unique depth from one view. New authored semantics require
format 35; earlier application versions may reject those files.

Native fallback face/edge handles identify the current complete B-rep. An
upstream geometry change invalidates an old fallback handle explicitly;
producer semantic names remain the preferred associative references. Smooth
native seams hidden by the edge renderer are excluded from pointer picking.
If a loft group such as `side.skin` identifies several distinct exact faces,
its group reference rejects as ambiguous; select a specific `side.skin.N` face.
Body mode keeps one active part; Ctrl/Shift accumulation applies to face,
edge and curve references.
Mesh patches use seed-relative 3-degree normal and `1e-4` mm plane checks,
with an explicit rejection above 4,096 connected source triangles. Curved
mesh faces retain triangle authority. Selection and hover do not edit the model.
Edge picking examines up to 64 nearby candidates within 11 pixels; zoom in
when densely overlapping edges make the intended target difficult to isolate.

Details: [images](IMAGE_AUTHORING.md), [Skeleton](SKELETON_AUTHORING.md) and
[indexed mesh edits](MESH_DIRECT_AUTHORING.md), plus
[native geometry and its measured limits](PARAMETRIC_GEOMETRY_019B.md).
