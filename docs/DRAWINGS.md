# Associative technical drawings (B.05)

022B adds [manufacturing sheets inside the shared CAD viewport](DRAWING_VIEWPORT_022B.md),
exact part/assembly/occurrence source recipes, an editable source catalog,
original vector templates, source-linked layout, exact section-region hatches,
centre marks and projected extent measurements. Read that contract for the
version-37 additions; the legacy feature-only contract below remains supported.

Agents create, edit, refresh or export drawings only when the user explicitly
requests that drawing work. Inspection and suggested catalog recipes are
passive reads; drawing creation is never an automatic design-completion,
BOM/DFM, document-load or viewport-entry step.

The legacy 021B scope authors persistent technical drawings linked to one exact
solid definition. Orthographic views derive visible/hidden edges and silhouettes
from OCCT `HLRBRep_Algo`; sections derive the exact solid/plane intersection.
Meshes and image pixels do not select these curves or their dimensions.

Call `agent_onboard{"topic":"drawings"}` and discover exact producer names with
`reference_inspect` before authoring. `doc_info` lists drawing names;
`drawing_inspect{}` lists their source feature IDs and observed bindings.

## Legacy native paper workspace

The compatibility native paper workspace renders a paper sheet from the
same inspected view curves and semantic dimensions. **New drawing** selects the
exact source definition, units, scale and paper size; it starts with a Top view.
**Edit sheet** edits those authored inputs. Changing units converts existing
declared tolerance deviations to preserve their physical size.

Use **Add view** or select a view and **Edit view** to set its name, direction and
paper position. Sections open a second form for their explicit source-local cut
origin. Renaming a view preserves its dimension associations. **Add dimension**
and **Edit dimension** use dropdowns populated by the current exact producer's
semantic names. Select the dimension kind, references, paper label position and
optional lower/upper deviations in the displayed units. Leave both deviations
blank to remove a tolerance. New dimensions use the existing producer's default
reference fraction 0.5; existing authored fractions are preserved by the forms.

The list shows measured values only while the source binding is current. Source
changes show **STALE**, hide obsolete sheet measurements and disable annotation
editing/export. **Refresh source** explicitly re-resolves the source at the
observed revision. Forms retain their revision and project identity while open;
intervening edits reject without a silent rebase. Close the workspace to edit the
3D model manually; connected agent edits remain visible through its state check.
**Export** publishes SVG or JSON to a new local file. **Delete sheet** is an
ordinary undoable document edit. Drawings are saved with the existing document.

The workspace is a presentation client, not a second geometry producer. Its
paper preview uses native GDI line/text rendering; the retained exact curves and
semantic measurements remain the API authority. It does not edit the B-rep or
use an edited SVG to select geometry.

**Zoom +**, **Zoom −** and **Fit** control a separate display zoom from 0.5× to
4×. They change neither the authored sheet scale nor the revision or measured
values. Fit shows the paper; zoom clips the paper to the preview viewport for
closer reading. Drag inside the paper preview to pan the zoomed sheet. Fit resets
both display zoom and pan without changing document authoring.

## Authoring and units

Use `drawing.set`, `drawing.refresh` and `drawing.remove` in `doc_apply` at the
observed `expected_revision`. Setting a drawing replaces its entire authored
object by unique name. The source accepts a persistent feature ID or a unique
feature name; the host stores the ID. There are at most 32 drawings, 12 views per
drawing and 128 dimensions per drawing. Unknown fields reject.

```json
{
  "expected_revision": 3,
  "commands": [{
    "op": "drawing.set",
    "drawing": {
      "name": "Bracket drawing",
      "feature": "Bracket",
      "units": "mm",
      "scale": 1,
      "paper_mm": [297, 210],
      "views": [
        {"name":"Top", "type":"orthographic", "direction":"top", "offset_mm":[45,70]},
        {"name":"Section A-A", "type":"section", "direction":"top", "origin_mm":[0,0,5], "offset_mm":[175,70]}
      ],
      "dimensions": [{
        "name":"Width", "view":"Top", "kind":"horizontal",
        "reference_a":"x_min", "reference_b":"x_max",
        "position_mm":[55,85],
        "tolerance":{"lower":-0.1,"upper":0.2,"units":"mm"}
      }]
    }
  }]
}
```

The example assumes those semantic names were observed on the selected solid.
Units are explicitly `mm` or `in`; tolerance units must match. `lower` is a
nonpositive deviation, `upper` a nonnegative deviation. These are caller
declarations, independent of geometric or manufacturing tolerance qualification.
Coordinates ending in `_mm` always use millimetres, including paper layout.
`scale` is paper/model scale in `(0,100]`; it never scales physical measurements.

Directions are `front`, `back`, `top`, `bottom`, `right`, `left`. Coordinates are
feature-local: front looks along local -Y with +Z up; top has +X right/+Y up;
right looks along +X with +Y right/+Z up. The opposite directions reverse their
horizontal or vertical axis to preserve a right-handed view frame. An orthographic
view has the feature origin; a section requires an explicit local `origin_mm`.
A section shows its intersection contour only; it is not a clipped orthographic
view of the remaining half of the solid.

`offset_mm` locates each view origin on the paper. `position_mm` locates a dimension
annotation. Dimensions are projected `linear`, `horizontal`, `vertical`, or exact
`radius`/`diameter`. Linear dimensions resolve two source reference origins at
optional `fraction_a`/`fraction_b` in `[0,1]` (default 0.5, using the existing
reference-frame producer's parameter convention). Projected coincidence rejects.
Radial dimensions resolve one exact circular `axis` with a measured radius and
require a view along that axis. There is no inferred rebinding to a nearby edge.

## Association, revision and persistence

The host owns `binding:{brep_base64_sha256,topology_sha256,observed_revision}`. Clients cannot
author it. The digest identifies the observed base64 B-rep bytes; it is not a
claim of canonical equivalence. The session revision is retained as provenance,
while the persisted exact geometry and semantic topology digests establish currency after reopen.
Metadata-only edits can preserve the same geometry binding.

A source geometry change preserves authored annotations and makes inspection
`stale`. Stale inspection omits derived views and numeric measurements. Missing
or suppressed source definitions, missing/ambiguous semantic references and
unsupported geometry report `invalid` with a reason. Neither state can export.
An explicit `drawing.refresh{name}` at the current revision derives the views,
resolves every dimension and updates the binding atomically; any failure leaves
the document/revision intact. Source deletion never silently binds a different
definition. Undo/redo and immutable history retain drawing authoring and bindings.

Legacy feature-only drawings use document version **36**. Version-37 recipes follow the linked 022B contract. Older documents keep
their applicable format. Save stores authored view/dimension inputs and binding,
without derived measurements, projected B-reps or display polylines. Invalid
associations remain readable for explicit repair. SQLite and JSON persistence
share this authoring contract.

## Inspection and local export

`drawing_inspect{name,expected_revision?}` returns `ark.drawing.v1`, `status`,
`authored`, and current revision. Current reports include exact projected curve
B-reps separately from `display_polyline_mm`, measured dimension values and the
measured source digest. The kernel accepts one valid exact solid with at most
256 faces/1024 edges, and rejects unbounded or unsampleable derived curves.
Preview sampling is bounded to 4096 points per curve/65536 per view, with a
16 MiB bound on the complete derived drawing view payload.
HLR superimposed lines are retained; no line deduplication claim is made.

`drawing_export{name,expected_revision,path,format:json|svg}` publishes a new
local file atomically without replacing an existing destination. JSON retains the
exact derived curves, source binding and measured dimensions. SVG contains the
views, dimension lines, units and declared tolerance labels as a presentation
preview sampled with nominal 0.025 mm deflection. It is not production curve
authority. Both exports disclose their authority and do not mutate revision.

## Limits and remaining B.05 work

This legacy scope is a partial B.05 implementation. The linked 022B scope adds assemblies/occurrence drawing sources, section hatches and centre marks, title blocks and original templates. Remaining work includes
detail/auxiliary views, arbitrary datum directions, clipped sectional views,
section labels/cutting-plane symbols, angular dimensions, GD&T,
ISO/ASME drafting compliance, drawing PDF/DXF/IGES
exchange and exchange readback qualification remain outside this scope.
The existing BOM remains a separate derived contract; it is not a second
editable quantity list or an embedded drawing table here. Production and
manufacturing qualification remain false.

## Local verification evidence

The native Windows test slice passed `associative-drawings`, `agent`,
`agent-features`, `iteration-history`, `document-container`, `modeling-workflow`
and `assembly-manipulation-tools` against the pinned OCCT 8.0.1 SDK. The drawing
test measures the actual top/front/section curve extents, exact semantic width/
height and inch diameter, and demonstrates independence from an emptied display
mesh. It exercises SQLite/history reopen, changed geometry/semantic topology,
revision conflict, lost references, source removal, undo, explicit consumed
definition IDs, mesh rejection and export no-replace behavior. A synthetic SVG
preview was inspected in the browser: top/front/section contours, dimension
lines, units and declared tolerance labels were visible and legible.

This evidence is local implementation verification. Full merged regression,
release packaging, distribution and manufacturing qualification require their
separate gates; no remote deployment or external adapter was tested here.

The basic Studio workflow additionally has real native control/form tests at
1180×900 and 1440×900. See [workspace verification](DRAWING_WORKSPACE_TESTING.md)
for the synthetic captures, commands, tested revision boundaries and limits.
