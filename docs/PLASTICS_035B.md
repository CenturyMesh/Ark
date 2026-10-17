# Plastics in 035B — B.02 / B.03

Status: **Verificado no escopo** local for the 035B Studio controls, native
recipes and synthetic checks below. Broader B.02/B.03 milestones remain
**Parcial**; this menu does not qualify mold manufacture.

The Plastics workspace groups work around an editable polymer part:

| Human control | Retained authority and useful limits |
| --- | --- |
| Start solid | Existing exact primitive or saved-sketch authoring. |
| Hollow walls | `native_solid` / `shell`, signed wall expression and explicit semantic faces to remove. Negative thickness hollows inward; unsupported offsets reject. |
| Release angle | Existing `draft`, pull axis, neutral plane and explicit faces. Existing planar/cylindrical/conical native domain applies. |
| Reinforce / fit | Existing 031B `rib`, `gusset`, `mount_pad`, `boss`, `screw_support`, `nut_seat`, `lid_lip`, `lid_groove`; one planar support, explicit coordinates and editable dimensions. |
| Vent / Round edges | Existing exact vents and edge treatment, with their published bounded contracts. |
| Separate | Existing plane sections: contour or explicit retained positive/negative/both sides. This is a geometric part separation operation. |
| Exact part / Edit recipe | Native measured B-rep evidence and editable expressions/references; downstream recomputation remains atomic. |

See [031B solid recipes](SOLIDS_031B.md) for support frame orientation, positive
footprint overlap, contained pocket requirements, disconnected-addition
rejection and semantic reference custody. The controls retain the source feature,
expressions and recipe instead of baking a display mesh. Manual dialogs capture
revision and document epoch before editing. Cancel preserves the model; stale
publication and native failure reject through the existing transaction boundary.

The menu and searchable command gallery expose the same actions. No drawing,
material-specific DFM acceptance or mold tooling is created merely by opening a
workspace. Focused remains the agent default. An agent should first inspect the
part and its exact support frame, choose explicit dimensions from the brief,
preview/apply at the observed revision, then inspect the measured native result.

## Product research and scope

The official Fusion documentation was consulted on 2026-10-17, as a capability
reference. No Autodesk icons, menu artwork, branding or screenshot content is
included in ARK.

- [Plastic rules](https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/SLD-SETUP-PLASTIC-RULES.htm)
  describes material and wall/draft/radius settings and a separate design-advice
  workflow. ARK does not claim an equivalent material-rule engine here.
- [Solid creation](https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/SLD-CREATE-SOLID-FROM-SKETCH.htm)
  describes Rib/Web and extension features including Boss, Snap Fit, Lip and Rest.
  ARK exposes its own bounded planar support recipes; it does not claim automatic
  nearest-wall ribs, two-body fastener synthesis, snap-fit analysis or parity.
- [Silhouette Split](https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/SLD-REF-SILHOUETTE-SPLIT.htm)
  derives separation from a view silhouette. ARK's existing plane section is a
  different, explicit operation.

Polymer part authoring and mold manufacture remain separate. Automatic parting
lines, shut-off/run-off surfaces, tooling split, core/cavity synthesis, shrinkage
compensation, ejector/runner/gate planning and mold-filling simulation are outside
035B. There are no decorative buttons suggesting these operations are available.

## Verification commands and required evidence

The new Studio probes operate only on synthetic scratch documents:

```powershell
ark-studio.exe --self-test-plastics-ui --shot out/035b/plastics.png
ark-studio.exe --self-test-electronics-workspace-ui --shot out/035b/electronics.png
ark-studio.exe --self-test-cable-guide-ui --shot out/035b/cable-guide.png
```

The Plastics probe uses actual ribbon mouse dispatch and native modal controls
for a hollow part, reinforcement, support attachment, retained recipe editing,
read-only inspection, cancellation and saved synthetic source. Its captures need
visual inspection. Existing native solid geometry tests remain necessary for
kernel geometry; a visible menu does not replace them. Final run outcomes and
remaining limits belong in the iteration receipt.

The Electronics workspace exposes board/pads, legacy rounded routes, exact
smooth 3D circular cables, flat ribbon sections, route guidance, native inspection
and Approved Vendors. The cable contract is documented in
[Electronics 035B](ELECTRONICS_035B.md).

## Observed local verification

All three Studio probes passed: Plastics, Electronics workspace and cable path
guidance. Captures were inspected at **1440 x 900**, including the native part
and round/ribbon scenes, both recipe/path and dimension/section modal steps,
readable vendor access, pending guide, protected driven point and saved bound
endpoint. The active guide hides the appearance floater so its HUD and handles
remain unobstructed. Other window sizes and display hardware are outside these
captures.

The path guide probe sends 100 mouse moves, observes coalescing, checks that the
source stays unchanged before release, publishes only the latest validated
candidate, and restores the gesture with one Undo. Cancellation, a changed
revision, driven expressions and retained occurrence-frame offsets are checked.
Undo comparison preserves exact JSON types, values and array order while ignoring
object key ordering, which the existing document serializer canonicalizes.

Per-run evidence records handler p95/p99, ten samples from a mouse event through
updated cage VBO upload, full frame draw and `glFinish`, and separate exact-worker
preview timings. These are measurements of one local synthetic fixture, including
a deliberately gated coalescing probe; they do not measure display-panel latency,
GPU timestamps or establish a general performance improvement. The iteration
verification receipt records the final run values and native test scope.
