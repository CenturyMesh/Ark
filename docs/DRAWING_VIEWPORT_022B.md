# 022B manufacturing sheets in the CAD viewport (B.05, A.01)

The existing CAD viewport switches between design geometry and source-linked
manufacturing sheets. Its rectangle, document, history and source selection are
shared. **Manage → Drawings** opens the sheet mode; **Design / 3D** or Escape
returns to geometry. The older native paper window remains a compatibility
client of the same drawing API.

Agents create, edit, refresh or export technical/manufacturing drawings only
when the user explicitly requests that drawing work. Drawing inspection and its
source catalog are passive reads. Model completion, BOM review, DFM inspection,
document loading and entering sheet mode do not create drawings automatically.
The human **All sources** control is an explicit authoring action.

## Exact sources and editable catalog

`drawing_inspect{}` retains `drawings`, `revision` and `ark.drawings.v1`, and
adds a `sources` catalog. Each row contains `name`, an explicit `source`,
`supported`, and either `reason` or `occurrence_count` plus an editable
`suggested_drawing` recipe. The catalog identifies final physical definitions,
definitions explicitly placed in the assembly, the document assembly root and
each authored assembly group. Unsupported geometry stays visible as a reason;
an exact source cannot be manufactured from a display mesh.

Sources are mutually exclusive with the legacy top-level `feature` field:

```json
{"kind":"part","feature":"Bracket"}
{"kind":"assembly","assembly":"Upper mechanism"}
{"kind":"assembly","assembly":0}
{"kind":"occurrence","node":"Repeated bracket","copy":2}
```

The host binds unique names to persistent IDs once. Assembly sources select an
explicit group or root; occurrence sources select an explicit stable copy slot.
The exact placed sources come from `resolveAssembly`, including supported
specialized patterns. A missing/skipped copy rejects rather than choosing the
seed or its nearest surviving neighbour. Hidden physical occurrences are
included; visibility is a display choice. Independent parts and their solids
are retained, without fusing or dropping them.

Part sheets use definition-local coordinates. Assembly and occurrence sheets
use solved world coordinates, also for their explicit section planes.
`source_occurrences` contains `feature`, `node`, `copy`, `path`, the rigid
`frame`, and exact B-rep/topology hashes. Assembly currency includes source
identity, hierarchy path, copy slot, exact geometry, topology and solved
placement. Canonical key ordering makes this binding stable through JSON and
SQLite/history reopen. The stored revision is provenance, not a substitute for
the persisted source digest.

## Original vector templates and annotation

`ark_precision` uses a light paper, restrained engineering ink and an asymmetric
title block. `ark_blueprint` uses a dark blue paper, pale curves and calibrated
annotation contrast. Both are original vector layouts authored in application
code. They share editable `title`, `drawing_number`, `revision_label` and
`notes`; no image asset selects geometry or measurements.

An `auto` layout fits exact view bounds into a source-linked paper grid with
reserved title-block and annotation space. Its derived presentation scale is at
most the authored `scale`; physical dimensions never change with display or
paper scale. Generated templates contain Top, Front, Right and an exact
isometric HLR view. View forms explicitly choose auto or manual positions and
preserves the current derived positions as editable inputs. Templates do not
imply a drafting standard or completed functional detailing.

`extent_horizontal` and `extent_vertical` dimensions measure the bounds of exact
projected B-rep curves. Their authority is `exact_projected_curve_bounds` and
they have no semantic reference fields. Existing linear, horizontal, vertical,
radius and diameter dimensions keep their exact semantic reference contract.
Assembly semantic dimensions declare `occurrence_a`/`occurrence_b` as
`{node,copy}` so each endpoint has an unambiguous exact producer and placement.
Radial dimensions still require a view along their measured circular axis.

Declared tolerance deviations remain optional, numeric and unit checked:
`{lower,upper,units}`. Templates supply none. The sheet states
**Tolerances: not declared** until a caller supplies them; the native typed
dimension form leaves both fields blank. Entering a label, template or tolerance
does not qualify manufacture. Angular tolerances, GD&T and full ISO/ASME
drafting compliance remain outside this contract.

Sections retain exact solid/plane contours. An exact solid/plane common creates
planar domains including holes; each hatch edge is clipped against those exact
domains. Section-region areas and actual hatch pitch/angle are disclosed.
Adjacent occurrences alternate hatch orientation. Hatch pitch is a bounded
presentation convention, not a manufacturing tolerance. Circular projected
curves have source-tagged centre marks. Section location remains explicitly
authored; a cut missing every source rejects.

## Shared viewport workflow

Parts and Assemblies tabs filter sheets, while Previous/Next and arrow keys
cycle them. New sheet chooses an exact catalog source and template. All sources
creates missing supported definition and assembly sheets in one observed
revision, retaining existing sheets and tolerances; an oversized transaction
rejects with an explicit command-bound message. Unsupported catalog entries
require review and their count is shown.

Sheet edits title-block inputs, units, paper size and auto/manual layout. Changing dimension units converts declared tolerance deviations to retain their physical size. Add view and Section
open typed forms with direction, position and explicit section origin. Dimension
and Edit dim expose exact producer references, occurrence identities and blank
tolerance deviations. Edit view preserves associations when renamed. Export
uses the existing atomic no-replace JSON/SVG publication contract. A drawing
line selects its source feature and concrete occurrence/copy in the shared
design selection; Design / 3D retains that origin selection.

Wheel, drag, Zoom +/− and Fit affect display zoom/pan only. They preserve the
authored paper scale, source, revision and measured dimensions. The sheet owns
the existing viewport canvas; sheet controls stay above it and the carousel
stays below it. Prior geometry-gizmo hit areas are removed from the sheet mode.

Source edits expose stale status and hide derived views and numeric dimensions.
Refresh is an explicit `drawing.refresh` transaction at the currently observed
revision. Native forms retain their observed revision/project identity and
reject intervening edits; no stored binding or annotation silently rebases.

## Agent transaction and persistence

After an explicit user drawing request, read `drawing_inspect{}` at the current
revision, edit the selected row's `suggested_drawing`, and submit it through
`drawing.set` in `doc_apply`. New and replaced recipes are evaluated and bound
atomically by the host. Later source changes require a fresh inspection and
explicit `drawing.refresh{name}` before exporting.

```json
{
  "expected_revision": 12,
  "commands": [{
    "op": "drawing.set",
    "drawing": {
      "name": "Mechanism / manufacturing",
      "source": {"kind":"assembly","assembly":"Mechanism"},
      "template": {"id":"ark_precision","title":"Mechanism","drawing_number":"M-01"},
      "layout": "auto",
      "units": "mm", "scale": 1, "paper_mm": [297,210],
      "views": [{"name":"Top","type":"orthographic","direction":"top","offset_mm":[0,0]}],
      "dimensions": [{"name":"Envelope width","view":"Top","kind":"extent_horizontal","position_mm":[0,0]}]
    }
  }]
}
```

New source/template/layout recipes, isometric views, occurrence endpoint
selectors and projected extent dimensions use `.ark` document version **37**.
Unchanged legacy feature-only drawings keep **36**. Application iteration
identity does not upgrade unrelated documents. Save persists authored recipes
and source bindings; measurements, exact projected curves, hatch curves and
display samples remain read-only derived results. There are at most 256 sheets,
12 views and 128 dimensions per sheet, and 256 exact source occurrences per
drawing. Worker limits are 4096 source faces, 16384 source edges and 4096 derived
edges per category; preview sampling and the 16 MiB derived report bound remain
explicit rejection boundaries.

## Verification scope

`ark-drawing-tests` retains the 021B contract cases and adds independently
calculated placed-box bounds, exact assembly/copy identities, hidden occurrence
retention, exact section area, annular-hole hatch clipping, current/stale/invalid
refresh boundaries, explicit-copy loss, source digest reopen and version-37
downgrade rejection. The native Studio flag
`--self-test-drawing-viewport-ui` drives actual main-window controls and writes
synthetic captures for parts, assembly section, selected origin, stale source,
refreshed sheet and returned design view. It checks passive opening, explicit
catalog creation, viewport bounds, typed form cancellation/revision conflict,
pan/zoom/Fit, source picking and Escape.

These are local implementation checks. B.05 remains **Parcial**: detailed
functional dimension review, auxiliary/detail views, clipped sectional views,
cutting-plane symbols, GD&T, drafting compliance, drawing PDF/DXF exchange and
exchange readback qualification still require their separate scopes. No
external deployment, production or manufacturing qualification is inferred.

The isolated native Windows build against the pinned OCCT 8.0.1 SDK passed
`ark-drawing-tests`, the integrated viewport test at **1440×1000** and
**1180×800**, and the legacy native paper-window regression at **1180×900**.
The resulting precision, blueprint, assembly-section and stale-state captures
were inspected. The sheet and controls fit the shared viewport at both tested
sizes; exact section contours/hatches, declared-unit extent labels, source
selection and hidden stale measurements were visible. The test logs and
synthetic captures are under `out/drawings-check/evidence` and remain local
verification artifacts. A merged release build and its wider tests have their
separate gate.
