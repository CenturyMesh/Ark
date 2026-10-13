# Test the 019B draft

The PR remains draft until the user tests and explicitly authorizes its merge.
Run the isolated executable; keep test changes in copies of existing drawings:

```powershell
& 'C:\Sw_templates\Ark-019B\out\test-preview\019B-test2\ark-studio.exe'
```

The synthetic example contains a dynamic Skeleton pattern, a helix and a loft:

```powershell
& 'C:\Sw_templates\Ark-019B\out\test-preview\019B-test2\ark-studio.exe' --script 'C:\Sw_templates\Ark-019B\examples\authoring-019B.arkops'
```

| Workflow | Check |
| --- | --- |
| Image reference | Attach or drop a PNG/JPEG in Assistant. Inspect its thumbnail. Switch **Inspire design / Image to CAD**; enter any subset of Width/Height/Depth in mm, or leave all blank. Send a request through an image-capable connector. Check the visible intent and supplied estimates. Remove/cancel must send nothing. |
| Solid operations | Select operands and use **Fuse bodies**, **Carve body**, or **Keep overlap**. Native failures must remain explicit. |
| Source face push | Create a native box. Activate **Push / pull source face**, drag its positive height/width/depth face, and use Tab to enter a value/expression. Inspect preview and dynamic dimension, then Apply. Escape cancels. Cylinders/cones support their positive planar height face. Other faces use **Source dimensions**. |
| Mesh editing | Import an indexed mesh, or explicitly use **Sample and weld mesh** on a native body. Try **Extrude mesh patch**, **Move mesh vertex**, **Move mesh edge** and Tab expressions. Inspect the source-linked `mesh_edit` history and closure. Save/reload, Undo/Redo, and change an upstream coordinate without changing its connectivity. |
| Body and curved selection | Select a body from the tree and inspect its halo. Pick a curved cylinder/loft face. **Select adjacent tangent faces** expands the bounded neighboring patch; exact semantic names remain available to agents. |
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

Details: [images](IMAGE_AUTHORING.md), [Skeleton](SKELETON_AUTHORING.md) and
[indexed mesh edits](MESH_DIRECT_AUTHORING.md), plus
[native geometry and its measured limits](PARAMETRIC_GEOMETRY_019B.md).
