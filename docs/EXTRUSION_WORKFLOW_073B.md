# Human extrusion workflow — 073B

**B.01 / B.02 / B.03 / D.02 — Verificado no escopo local.** The sketch,
exact-solid and surface Extrude entry points share one native editor. Select
an existing sketch, enter driving expressions, choose **Solid**, **Surface**
or **Thin wall**, then review the exact candidate before **Apply**.

The editor provides depth, start offset, forward/reverse direction, symmetry,
result mode and result name. Wall thickness and side enable only for Thin wall;
target-body selection enables only for Join targets. Surface uses New body.
The form scrolls and keeps keyboard focus usable on a constrained work area.

## Regions and results

`sketch_inspect{name,native_regions:true}` supplies the exact OCCT region list.
For several closed regions, check the intended regions by name; holes follow
their outer loop. Each selected region becomes a separate editable extrusion
feature in one atomic batch. One Undo restores the state before that batch.
Single-region sources need no extra selection dialog. Open profiles are
available to surface and supported thin-wall extrusion.

**Join targets** offers exact solid definitions, including an explicit multiple
target selector. The batch retains the extrusion sources and adds an exact
union. The result must satisfy the producer's connected-solid requirements;
the current union contract accepts up to sixteen targets and extrusion results
combined. No source solid is silently dropped. Joining placed assembly
occurrences is outside this editor's scope. In an assembly drawing, a new
separate extrusion can create an explicit new occurrence in document
coordinates or remain a definition only.

The preview shows only the resulting bodies, including every result of a
multiple-region operation. Its bitmap is generated from the actual checked
candidate's display meshes; native B-reps and their measured reports remain
the authority. The report includes exact native volume or area. Cancel leaves
authoring unchanged. Apply uses the retained candidate and its original
revision/epoch; a concurrent drawing change rejects the stale editor.

## Offset, thin walls and editing

Start offset follows the chosen, already directed extrusion axis. Symmetric
extrusion is centered on that offset plane. Depth, offset and thickness retain
their named-parameter expressions. Surface distance is signed explicitly when
Reverse is selected. A selected closed surface region creates the walls of
its outer and hole contours, through `native.region_walls:true`.

Thin-wall sides are **Inside / left**, **Outside / right** and **Centered**:
inside/outside refer to closed profiles, left/right to supported open paths.
The native producer currently supports closed simple straight polygons and
circles without holes, and open line chains or a single arc. Collapsed,
self-intersecting or otherwise unsupported offsets reject explicitly.

Editing a current sketch extrusion retains its feature ID. Existing legacy
features remain editable through their existing source editors. For supported
NativeSolid recipes authored through the CLI, numeric/expression fields in
`native` populate the controls when `p` has no override; applying the editor
normalizes controlled fields into `p` and removes competing native aliases.
Surface scalar fields follow their existing `p` contract. Cancel does not
normalize or rewrite either representation. Switching Thin wall to Solid
removes thickness and thin-side fields. Unchanged direction, symmetry and
surface solid-state expressions are retained. Unused literal-zero start offset
and surface symmetry do not introduce unnecessary new semantic fields.

## Local verification

The first 073B UI checkpoint passed **5/5 suites**: extrusion workflow at
1440×900 and 1180×800, editor scrolling, feature-editor picking and existing
direct authoring. Each new workflow run passed **97 checks**, including a
960×640 work-area/focus probe. Real dialog captures were inspected using their
owned-window images, with selected-region, multiple-body, surface-wall,
thin-wall, target-union and CLI-source edit controls and previews visible.

Synthetic geometric checks include a holed disk and a separate island,
analytic union volume, open surface area and reverse/symmetric offset bounds,
open/closed thin-wall volumes, thin-to-solid editing and expression retention.
Cancellation, one-step Undo and stale-revision rejection are covered. Fixtures
contain no user drawings. Final release-wide verification is recorded in the
iteration delivery notes; this document does not claim occurrence-aware joins,
general thin-wall offset support or manufacturing qualification.

Reproduce with `--self-test-extrusion-workflow-ui`. Local captures and receipts
are under `out/release-068B/ui-checks/extrusion-{normal,compact}-073b.png.*`.

The legacy Sketch Extrude/Cut flow is also covered by
`studio-sketch-extrude-cut-073b`. Cut selects the existing exact profile-hole
producer for B-rep bases and preserves the mesh-cut path for mesh bases. Native
profile cuts reopen in the four-field editor; cutter expressions and identity
are retained. Depth edits and a subsequent width edit match analytic volumes.
