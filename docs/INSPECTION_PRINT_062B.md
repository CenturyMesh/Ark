# Smart inspection and 3D print jobs

Press **D**, hover an edge or face, and click. **Ctrl-click** adds or removes
entities; an ordinary click starts a fresh set. The panel shows native length,
diameter/radius or area. Two selections show minimum distance and applicable
angle; three or more show total selected edge length. **D/Esc** closes the tool.
Use **Units** to change displayed units without changing model dimensions.

Under **Inspect → Section**, choose **Plane** or **Draw line**. Plane accepts
XY, XZ, YZ and named construction planes. Draw line uses two clicks across the
viewport. The preview stays in world space when orbiting. **Flip**, **Offset**
and **Close** control the view. A section changes only inspection; exporting
the design still exports its complete source geometry.

To isolate several parts, **Ctrl-click** them in the viewport or model tree,
then choose **Spotlight**. Exit Spotlight restores the previous display set.

For a project print job, first declare each intended part's additive route,
material grade, build direction and slicer profile. **Manufacturing → Print
job** gathers project quantities and asks for the printer build volume,
spacing and margin. **Plates** displays the prepared arrangements. The agent
uses the same preparation contract:

```json
{
  "action": "prepare_print_job",
  "expected_revision": 12,
  "job": {
    "name": "Enclosure build",
    "bed_mm": [256, 256, 256],
    "gap_mm": 6,
    "margin_mm": 5
  }
}
```

The example volume must be replaced with the actual usable printer volume.
When `items` is omitted, all declared additive terminal outputs or assembly
occurrences are counted, including hidden occurrences. Optional `items` entries
specify `feature`, `quantity`, `material`, `profile`, `cut_group` and
`build_direction`; omitted declarations are taken from that source feature.
`cut_group` groups already separated parts; it does not cut the CAD model.

The response contains either blockers or `preview_arguments` for `doc_preview`
and its `doc_apply`. No partial job is published on a blocker. Each retained
plate is a manufacturing plan with exact source hashes, rigid item placements
and a `pending_slice` operation. Configure matching real profiles and use
`manufacturing_slice` for each plate, then import the resulting G-code using
`manufacturing_plan` action `import_gcode` (`operation: "op1"`) before simulation.
The existing Bambu integration never sends a job to a printer.

See [062B evidence and limits](iterations/062B.md).
