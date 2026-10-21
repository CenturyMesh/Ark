# Parametric HUD — 048B

Milestones **B.01**, **B.02** and **D.02** remain **Parcial**. The Parametric
HUD lets a human change the dimensions that drive a model from a translucent
overlay in the Studio viewport. ARK then rebuilds the model. An agent can make
the same change with `doc_apply`. Each change is an ordinary `param.set`
transaction: it carries the observed `expected_revision`, it is published
atomically, and the iteration history records the actor. The HUD never edits
geometry directly. The B-rep produced by the feature history stays the
geometric authority.

## Contract: HUD metadata on named parameters

`param.set` accepts an optional `hud` object. `hud:null` removes the exposure
and keeps the parameter. `expr` may be omitted when the command only changes
the HUD of an existing parameter.

```json
{"op":"param.set","name":"width","expr":60,
 "hud":{"label":"Width","min":20,"max":120,"step":5,"unit":"mm","group":"Body","order":1}}
```

| Field | Rule |
| --- | --- |
| `label` | 1..64 bytes; the parameter name is shown when it is absent |
| `min`, `max` | finite numbers of magnitude at most 1e8, each optional; when both are present, `min < max` |
| `step` | `> 0` and not larger than `max-min`. Default: 1, or a power of ten close to a twentieth of the range when both bounds are present and the unit is not count |
| `unit` | `mm`, `deg`, `count` or `none` (default). Display metadata only: values stay in ARK units. `count` needs integer bounds, step and value |
| `group`, `order` | grouping text of 0..64 bytes, and an integer from -1000 to 1000; rows sort by group, order, name |

At most **64** parameters can be exposed. The stored object is canonical
(sorted keys, `unit` and `step` always present), so iteration history and file
round trips are byte-stable.

The `hud` field extends `param.set` rather than adding an operation. It is a
property of the same named parameter, so a batch can declare a driver and its
range in one command. It also travels through existing paths without new code:
`doc_preview`, design-container merges (`hud:null` when a merge removes it) and
team proposals.

### Typed rejections

All of these reject the whole batch atomically; the document and revision stay
unchanged.

| Code | Cause |
| --- | --- |
| `HUD_INVALID` | unknown field, wrong type, non-finite or out-of-bound number, `min >= max`, `step <= 0` or `step > max-min`, bad unit/label/group/order, non-integer count bounds or step |
| `HUD_PARAMETER_NOT_FOUND` | `hud` without `expr` on a parameter that does not exist; an unknown name in a read tool |
| `HUD_CAPACITY` | more than 64 exposed parameters |
| `HUD_VALUE_OUT_OF_RANGE` | after the batch, a new or changed HUD parameter evaluates outside `[min,max]` |
| `HUD_VALUE_NOT_INTEGER` | a changed `count` parameter is not an integer |

ARK never clamps an agent value. The range check runs after every command of
the batch is applied and before geometry is evaluated. It covers every changed
HUD parameter, including a driven one whose expression moved it out of range.
A row whose value and metadata did not change is not rechecked. A document
saved with an out-of-range value therefore still loads and accepts unrelated
edits; `hud_info` reports `in_range:false` for that row.

A value inside the range can still fail to rebuild (for example
`Feature #1: dz must be positive`). That failure is the ordinary atomic
`doc_apply` rejection: the previous geometry, values and revision stay intact.

### Persistence

The canonical object is stored as `params[].hud` in `.ark`, `.arkjson` and the
iteration journal. It does not select a document format version; see
[formats](FORMATS.md). Documents without HUD metadata load unchanged. Undo and
redo restore HUD metadata with the parameter values because both live in the
authored parameter row. A pre-048B runtime rejects a journaled document that
carries `hud` instead of silently dropping it (established by code reading,
not by running an older binary).

## Studio

**Model > More > HUD** or **Manage > HUD** toggles the overlay. It sits in
the upper right of the viewport. It stays below the view cube when the
viewport is narrow, above the navigation triad, and clear of the viewport
display dock. It moves down when the *Inspect failed operation* button is
shown. The header has **-** (collapse to a one-line pill) and **x** (hide;
staged values are kept).

Rows are grouped by `group` and sorted by `order`. Each row shows:

- the label;
- the value with its unit;
- **-** and **+**, which move to the previous or next point of the step grid
  anchored at `min`;
- a value box that opens a typed entry. A number or an expression of other
  parameters is accepted;
- the range and step hint.

A typed or stepped value outside the range is clamped in the HUD, and the
result line names the limit, for example `Clamped to the HUD maximum 120 mm`.
A `count` value is rounded to an integer. Driven parameters, whose expression
references other parameters, are shown read-only with their expression.

**Staged edits.** By default each change is staged and highlighted in amber,
with `was <value>`. **Rebuild (n)** publishes all staged values as one
`doc_apply` with `actor:{kind:human,id:studio}` and `expected_revision` set to
the revision observed when the first value was staged.

If another writer (for example an agent) committed in between, the HUD does
not publish. It reports the revision change and keeps the values staged; a
second **Rebuild** applies them on the current model, and **Reset** discards
them.

The result line shows the measured wall time of the transaction and scene
refresh, the document recompute time and the new revision. A rejection is
shown in red with its error, and the staged values stay for correction.

**Auto** publishes each change at once through the same path. **Reset**
discards staged values without touching the model.

**Agent edits.** Rows read the committed document on every frame, so a
`param.set` from an agent appears immediately. A staged value stays staged, and
its `was` hint shows the agent's new value.

**Show in HUD.** In the Model explorer, each Parameters row has a chip on the
right: `+HUD`, or `HUD` when the parameter is already exposed. The chip opens
a native dialog with Label, Minimum, Maximum, Step, Unit, Group, Order and a
*Show in HUD* toggle. Applying it sends one human `param.set{name,hud}`, or
`hud:null` when the toggle is cleared. When no parameter is exposed, the HUD
offers *Show a parameter...*, which uses the same dialog with a parameter
choice.

The overlay uses the existing OpenGL panel primitives at physical pixels, like
the viewport dock and the Baked Motions panel. Widths derive from the viewport
(270..340 px) and labels are fitted with the renderer's glyph advances, so
text is shortened with an ellipsis instead of clipped. Rows beyond the
available height scroll with the wheel or the ^/v controls.

## Agent tools

Both tools are read-only (`readOnlyHint:true`). They are therefore available to
every team role, including reviewers. In the assistant catalogue they are
specialist tools of topic `hud`: they are offered after `agent_onboard{topic:"hud"}`,
and the `agent_onboard` description lists them.

`hud_info{names?,expected_revision?}` returns, per exposed parameter: name,
label, group, order, unit, expr, value, display, min, max, step, `in_range`,
`driven`, `dependents` and `downstream_feature_ids`.

- `dependents` lists `{id,name,via}`. `via` is `expression`,
  `parameter:<name>` (through a dependent parameter), `sketch:<name>` or
  `plane:<name>` (a sketch or datum whose expressions reference the
  parameter).
- `downstream_feature_ids` are later features that consume dependents as
  inputs and are rebuilt too.

`hud_flex_check{names?:1..16,values?:[min|mid|max],max_evaluations?:1..96,max_seconds?:1..120,expected_revision?}`
evaluates each selected parameter alone, with every other parameter at its
current value. It samples `min`, `max` and `mid` (snapped to the step grid).
Each sample runs on a scratch session through the same `doc_apply` validation
that `doc_preview` uses, never committing.

- Per sample it reports `status` `pass`, `fail` or `not_evaluated`, the
  value, `ms`, and for a failure the `error`, `error_code` and
  `failing_feature{id,name}` or `failing_sketch`.
- Driven and unbounded parameters are listed in `skipped` with the reason.
- The defaults are 48 evaluations and 30 s. Samples beyond either bound are
  reported as `not_evaluated`, and the report sets `complete:false`.
- The report carries `sampled_values_only:true`, `committed:false` and a
  `scope` sentence. It covers only the sampled values, one parameter at a
  time; it is not a proof for the whole range, for unsampled values or for
  combinations of parameters.

## Agent protocol (`agent_onboard{topic:"hud"}`)

Use it only when the user explicitly asks for a HUD, adjustable dimensions or a
parametric design. The overview guide mentions it (`parametric_hud`) without
imposing it on normal designs.

1. Declare the driving parameters first, with HUD metadata and ranges in which
   the design stays coherent.
2. Make every driven dimension an expression of those parameters. Derived
   dimensions are expressions too, never literal copies.
3. Run `hud_flex_check` and fix every failure before reporting. Change
   expressions or the declared range; do not widen tolerances or drop solids.
4. Report the HUD parameter list from `hud_info` and the sampled scope.

## Local evidence (diagnosis toolchain)

Recorded on the local MinGW diagnosis build with unpatched OCCT, which is not a
release toolchain. This is local proof only. It is not evidence of
integration with other products, of deployment, of production use or of
manufacturing qualification.

- `ark-parametric-hud-048b-tests` (CTest `parametric-hud-048b`) covers:
  - validation and every typed rejection, with the document and revision
    unchanged;
  - the boundary value, `hud:null`, and `doc_preview` rejecting an
    out-of-range candidate;
  - the 64-parameter capacity;
  - `.ark` and `.arkjson` round trips, version independence, a legacy
    document without HUD, an invalid stored HUD, and a stored out-of-range
    value;
  - undo and redo of HUD metadata and values, and the human actor in
    `history_info`;
  - a failed rebuild that keeps the JSON, revision and bodies, and a two-value
    batch that publishes once;
  - `hud_info` dependents through expression, parameter and sketch, plus
    driven and unused rows;
  - `hud_flex_check`: 12 samples with one failure naming feature #1, budget
    reporting, subsets, an invalid selector, the fixed model passing, and the
    refresh after an agent edit;
  - onboarding, catalogue and topic discovery.
- Studio `--self-test-parametric-hud-ui` (CTest `studio-parametric-hud-ui`,
  1280x800, plus a 1100x700 pass) covers:
  - the ribbon toggle;
  - grouped rows, with step controls absent on the driven row;
  - two staged edits that leave the document unchanged, then a single-revision
    Rebuild with the human actor and the measured time;
  - a typed value clamped with the limit shown, and Reset;
  - a failed rebuild that keeps the model and revision and shows `Feature #1`;
  - Auto;
  - staging from the value after an agent edit;
  - Show in HUD from the explorer chip, and collapse;
  - bounds against the view cube, the triad and the viewport dock.

  Screenshots: `.parametric-hud-open.png`, `-staged`, `-failed`, `-collapsed`
  and `-narrow`.
- Full local CTest run: 145 of 150 passed. Three failures are known on this
  toolchain: `native-solid`, `native-surface` (both need the patched OCCT) and
  `distribution-fixtures` (cmake not on PATH). The other two, `ota-updates`
  (temp-directory access denied) and `assembly-scaling-native-029b`, passed
  when rerun serially.
- Adding the two tools moved the catalogue from 102 to 104 tools in the
  feature branch. With the exploded-view tools of the same iteration the
  catalogue has 106 tools, and the eight exact-count tests assert 106.
- Other Studio selftests run, each exiting 0: `baked-motions-ui`,
  `viewport-dock-ui`, `history-ui`, `realtime-ui`, `action-icons-ui` and
  `navigation-ui`.
- An `ark-mcp` stdio smoke run with a native OCCT `native_solid` box covered
  these steps:
  - `tools/list` lists both tools with `readOnlyHint:true`;
  - `agent_onboard{topic:hud}` returns the guide;
  - a `doc_apply` declares two HUD drivers;
  - `hud_info` lists the dependents;
  - `hud_flex_check` reports the minimum height failing in feature #1 with
    the OCCT error;
  - an out-of-range `doc_apply` returns `HUD_VALUE_OUT_OF_RANGE` with
    `document_unchanged:true`.

## Not established

- **Sampling.** `hud_flex_check` samples three values per parameter, one
  parameter at a time. It does not prove validity across the range or for
  combinations, and it does not search for the failure boundary.
- **Clamping and units.** The HUD clamps human input; agents are rejected.
  Units are labels only, with no unit conversion.
- **Rebuild time.** The time shown is measured locally. It includes the scene
  refresh and is not a performance guarantee.
- **Dependency inventory.** It is lexical over authored expressions and
  sketch/datum names. It does not trace native-spec references that name
  sketches indirectly, or assembly placement expressions.
- **Agent tooling.** `query_batch` does not yet accept `hud_info` or
  `hud_flex_check`; call them directly. The team role tool lists pick them
  up through `readOnlyHint`; no team-specific code was added.
- **Older runtimes.** Their behaviour with HUD documents is inferred from
  code, not tested.
