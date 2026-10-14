# Component patterns in 022B

Scope: **B.04**, with **B.01/B.02** authoring and persistent source/history
contracts. These milestones remain **Parcial**. The implementation supports
source-linked rigid component placements; it does not claim complete assembly
configuration, physical chain contact, a motion study or manufacturing qualification.

The screenshot's six component-pattern choices map to these saved recipes:

| Human command | `pattern.type` | Driving source |
| --- | --- | --- |
| Linear Component Pattern | `linear` | Expression count and parent-frame `dx/dy/dz` |
| Circular Component Pattern | `circular` | Expression count, degree step and parent X/Y/Z axis |
| Pattern Driven Component Pattern | `pattern_driven` | An existing patterned occurrence |
| Sketch Driven Component Pattern | `sketch_driven` | Ordered named planar points or actual spatial curve handles |
| Curve Driven Component Pattern | `curve_driven` | An exact native edge or connected native curve path |
| Chain Component Pattern | `chain` | The same exact guide with one or two alternating seeds |

Hyphenated `pattern-driven`, `sketch-driven`, `curve-driven` and
`chain-component` aliases normalize to the canonical stored names. The
`*_driven_component` and `chain_component` aliases are also accepted.

The vocabulary follows the official component-pattern families. The official
[sketch component pattern](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_DVE_COMPPAT_SKETCH.htm)
uses point geometry from 2D or 3D sketches; the ARK spatial scope is its actual
authored XYZ curve handles. The official
[curve component pattern](https://help.solidworks.com/2025/english/SolidWorks/sldworks/r_curve_driven_comp_pattern_pm.htm)
offers curve spacing/alignment, and
[chain patterns](https://help.solidworks.com/2025/english/SolidWorks/sldworks/c_chain_component_pattern.htm)
include several linkage domains. ARK's scope here is static distance-along-guide
placement and alternating rigid seeds. Connected-linkage simulation and each
product's other options are not implied by the command name.

## Authoring and stable identities

Studio's component-pattern commands use the same `assembly.add`/`assembly.edit`
contract as agents. Forms select an existing seed occurrence or an explicit new
occurrence definition, then configure source, spacing, orientation and skips.
Editing/rebinding preserves the node ID, parent, other placement fields and mates.
The remove-pattern command retains copy 0 and removes the generated copies.
The evidence command reports the exact source length, pitch, endpoint gap and
fit measurements available for that occurrence.

Every `doc_apply` must use the currently observed `expected_revision`.
Validation/evaluation precedes publication; failure leaves the document and
revision intact. One node owns the pattern and the mate solver carries it as
one rigid set. BOM and rendering count every emitted copy, including alternate
seed definitions. IDs are `(node,copy)` with slots `0..31`. A `skips:[1,3]`
recipe omits those slots without renumbering others. Copy 0 is mandatory and
keeps its authored/solved pose. Source stations and transforms are relative to
the source's station/copy 0; changing the source's absolute origin does not
silently rebase the seed.

Count is an expression from 2 to 32. Pattern/sketch-driven counts can be
omitted to follow the source's logical slot count. If supplied, count must
match that source count. Source skips propagate through pattern inheritance;
target skips add omissions. A skipped tail slot remains part of the logical
source count and can reappear with its original identity.

Source feature/occurrence unique names bind once to IDs during authoring.
Renaming a producer retains the link. Point handle names remain semantic names;
deleting or replacing a requested handle rejects. Rebinding is an explicit
recipe edit. Missing/suppressed producers, non-rigid frames, cycles, duplicate
point names, coincident stations and invalid/overrun spacing reject.
Caps are 32 slots per pattern, 32 guide edges, 64 inheritance levels,
4096 assembly nodes and 65536 expanded placements. The smaller slot cap retains
the existing copy-selection/mesh-cache contract.

## Source recipes

```json
{
  "expected_revision": 12,
  "commands": [{
    "op": "assembly.edit",
    "id": "Fasteners",
    "pattern": {
      "type": "pattern_driven",
      "source": {"node": "Driving row"},
      "skips": [2]
    }
  }]
}
```

Pattern inheritance uses the source's resolved world-space copy transforms
relative to copy 0, including rotated parent groups and solved mate poses.
The target's mate solution carries the inherited set rigidly. The source must
own an active pattern; a source cycle rejects explicitly.

Planar sketch source:

```json
{"type":"sketch_driven","source":{"sketch":"Stations","points":["a","b","c"]}}
```

Only actual named `point` entities are selected; `point.a` is an accepted name
alias. Coordinates come from the evaluated sketch and its construction datum,
not a display polyline. The ordered first point corresponds to copy 0. Sketch
coordinate expressions and datum transforms remain source-owned and editable.

Spatial sketch source:

```json
{"type":"sketch_driven","source":{"feature":"Spatial guide","points":["start","segment.1.end","segment.2.end"]}}
```

The feature must be an authored native `curve` with `start`/`segments`.
`solveSpatialCurve` provides the same solved XYZ handles used by the producer,
including its constraints, expression coordinates and anchored handles.
Available names include `start`, `segment.N.end`, `segment.N.through`, and
actual spline `segment.N.point.M` / NURBS `segment.N.pole.M` handles. This does
not substitute XY points or tessellation for a 3D sketch.

Curve source:

```json
{
  "type":"curve_driven",
  "count":"station_count",
  "source":{
    "feature":"Guide",
    "reference":"path",
    "spacing":"equal",
    "alignment":"tangent",
    "normal":["0","0","1"],
    "margin":"0",
    "closed":false,
    "endpoint":"include"
  }
}
```

`reference` selects one exact producer edge such as `edge.0`, or `path` for
all 1..32 ordered edges of a native curve/intersection/bridge. A full path must
be connected and unbranched within the fixed `1e-6 mm` endpoint tolerance.
Native edge traversal may reverse a selected edge to retain the connected
path direction. Disconnected/ambiguous/non-adjacent shared endpoints reject;
edges are never discarded. Source geometry uses feature-local coordinates
in the pattern parent's coordinate frame, independently of placed instances.
A source depending on assembly occurrences is outside this contract and
rejects; a self-consuming source explicitly reports a dependency cycle.

`spacing:"equal"` divides the usable arc length. `spacing:"pitch"` requires a
positive expression `pitch` in mm. `margin` is an expression at both ends.
Open `endpoint:"include"` reaches the final usable station for equal spacing;
`exclude` uses the count as denominator and leaves the end station out.
Closed paths require `closed:true`, zero margins and `endpoint:"exclude"`.
Native endpoint closure is measured. A fixed pitch additionally requires
`count*pitch` to match the full length within `1e-6 mm`; no duplicate seed
station or implicit wrap is produced.

`alignment:"fixed"` retains the seed orientation. `tangent` applies each native
tangent/declared-normal frame relative to station 0. Frame X follows the exact
station tangent; frame Z is the declared normal projected perpendicular to
that tangent. The normal accepts three expression coordinates. Degenerate
tangents and parallel normal/tangent pairs reject. At a sharp multi-edge join,
the arriving edge defines the station tangent; no smoothness or articulated
joint is inferred.

The existing native geometry-query worker integrates/inverts B-rep arc length
and evaluates point/tangent derivatives at the requested stations. Its
inversion tolerance is `1e-8 mm`; this is numerical kernel evidence, not a
global analytical error proof. There is no mesh-position interpolation.
Helix/equation native fits require explicit positive `max_source_error <=1 mm`.
Observed source checkpoint error and available kernel fit error must both fit
that bound. Reports retain that global between-checkpoint error is unproved.
The implementation does not enlarge tolerances to make a source succeed.

Chain source adds these optional fields to the curve recipe:

```json
{"alternate_feature":"Outer link","alternate_angle":"90"}
```

Alternatively `alternate_node` chooses a distinct unpatterned second occurrence,
retaining its actual relative seed pose; it cannot coexist with
`alternate_feature`. The second definition shares the primary origin convention
unless a second occurrence provides its offset. Odd slots use the second seed
and an expression rotation about its local X axis. Default angle is 90 degrees
for a definition/same-seed pattern and zero for an existing alternate occurrence.
Two-definition closed chains require an even count. Pitch is distance along
the guide; rigid-link contact, pin engagement, chord-length linkage closure,
clearance, articulation, dynamics and material strength are explicitly unchecked.

## Persistence, diagnostics and verification

Specialized types, source recipes or skips require document version **37**.
Older declarations reject those fields instead of interpreting them as legacy
patterns. Ordinary linear/circular recipes without source/skips retain their
prior version. This format change is for persisted authoring data, independently
of release identity. Source, expressions and skips survive save/load/undo/redo.
Only source recipes persist; derived placements and query caches do not.

`assembly_info.diagnostics.patterns` reports emitted count, stable slots and
source link evidence. Curve/chain rows add exact guide length/edge count,
endpoint gap, declared spacing/pitch, alignment, sampled frame count,
arc-length numerical tolerance and applicable source fit measurements.
Closed rows expose `closure_pitch_residual_mm`. Chain rows explicitly report
`link_contact_checked:false`, `motion_checked:false` and
`manufacturing_qualified:false`.

The geometry query cache owns full B-rep bytes, full semantic topology and
the exact query payload. It is thread-local, bounded to eight source identities,
64 responses per identity and 16 MiB of serialized identity/response data. Revision or node IDs alone never authorize
cache reuse. Metadata-only copy slot discovery does not solve/sample geometry.

Meaningful acceptance corpus: `tests/specialized_pattern_tests.cpp` covers
rotated-parent inherited transforms, recursive propagation, trailing skipped
slot restoration, source rename/rebind, expression recomputation, cycles,
missing sources, revision rejection, atomic rollback, format-37 rejection,
history, real spatial XYZ handles, whole-wire native arc-length stations,
tangent orientation, alternate link definitions, closed endpoint/pitch checks,
closure evidence and explicit helix source fit limits.

Verification status: the isolated Clang C++20/OCCT build and
`ark-specialized-pattern-tests` passed locally on 2026-10-14. The corpus also
checks an offset seed under a rotated parent, an offset second seed with its
actual orientation, and rigid-mate carry of the exact curve placements.
The initial run exposed pre-evaluation history parsing and tangent pivot errors;
both were corrected and the expanded corpus passed.

The isolated Studio `--self-test-specialized-pattern-ui` run passed with exit
code 0 at 1440×900 on 2026-10-14. It clicks the actual Tool Gallery controls
and fills native typed dialogs for all six families, edit/rebind/remove,
cancel, stale revision/document-epoch rejection, inferred counts, skipped
slots, alternate occurrence selection and agent attachment, and read-only
pattern evidence. Captures of the dialogs, all-family scene, closed chain and
evidence were inspected; shortened labels fit the controls. The probe exposed
consumers that assumed the primary definition for alternate copies; actual
placement identity is now used and the expanded consumer/UI corpus passed.
Local evidence is in `out/evidence-pattern-ui/`, including
`patterns.png.specialized-pattern-ui.log`. The synthetic fixture creates no
2D drawings. These checks establish local authoring and UI behavior within
the documented domains, without production deployment or manufacturing
qualification.
