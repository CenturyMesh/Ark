# Design variants and parameter exploration — 067G

Scope: **B.02/B.04/D.05/G.03 — Verificado no escopo local**. See the [iteration evidence](iterations/067G.md); broader milestones remain **Parcial**.

The Studio **Design variants** window configures an installed parametric program.
**New family** installs one of the existing editable examples. The parameter list
shows authored values, units and declared bounds; **Edit value** stages a numeric
value. **Preview** evaluates the staged configuration without publishing it;
**Apply** prepares and publishes one revision-bound exact candidate. Undo restores
the previous editable source.

**Save named variant** records a full parameter map, namespace and program-source
digest in the document. These are authored configurations, not cached B-reps.
The `.ark` file uses semantic version 56 only when this new data is present.
Saved values survive reopening. A changed program digest is explicitly stale;
it cannot silently apply to different program source.

**Compare saved** evaluates every named alternative in an isolated session. The
window shows exact dimensions, summed terminal-part volume and mass when all
terminal parts have declared material densities. Three sampled mesh thumbnails
can be viewed side by side. Selecting a result loads its values for explicit
Save or Apply. Triangles are display evidence only and never select the production
geometry or measured dimensions.

**Explore values** evaluates explicitly entered values for one parameter in the
native UI. Optional width/depth/height/mass limits filter feasibility. Explicit
feature pairs and a minimum gap use native B-rep distance/common-volume inspection
for each trial. Overlap, missing features or unavailable measurements cannot rank
as feasible. The Studio form accepts one pair; the tool accepts up to sixteen
through `clearance_pairs:[{a:"Left",b:"Right",min_mm:3}]`.

The tool also supports a Cartesian grid of 1–4 parameter axes, with at most 64 combinations.
This is a bounded enumerator, not continuous or global optimization. Every trial,
including compile, assertion and geometry failures, remains in the result.

## Agent contract

`design_variants` exposes `inspect`, `save`, `remove`, `evaluate`, `explore` and
`prepare`. Except for inspection, every action requires `expected_revision`.
Save/remove/prepare return a `doc_preview` candidate; the caller explicitly sends
`doc_apply {candidate_id, expected_revision}` to publish it.

```json
{"action":"evaluate","namespace":"shelf","expected_revision":12,
 "variants":[
   {"name":"Compact","values":{"width":500,"shelves":3}},
   {"name":"Standard","values":{"width":800,"shelves":5}},
   {"name":"Wide","values":{"width":1100,"shelves":5}}],
 "constraints":{"width_mm":{"max":1000}},"objective":"volume_mm3",
 "direction":"min","thumbnails":true}
```

```json
{"action":"explore","namespace":"shelf","expected_revision":12,
 "axes":{"width":[500,800,1100],"shelves":[3,5]},
 "constraints":{"height_mm":{"max":1900}},"objective":"volume_mm3"}
```

Values and axes follow the program's [parameter kinds](PARAMETRIC_PROGRAMS.md#parameter-kinds):
numbers, `true`/`false` or `0`/`1` for a boolean, and only a listed choice,
including choice text, for example
`"axes":{"finish":["Oak","Pine"],"rail":[false,true]}`. An unlisted value is a
retained failed trial. Results and saved variants store resolved values: `1`/`0`
for a boolean and the listed text for a text choice. A saved text value requires
semantic version 60; numeric-only variants keep version 56. Studio's
**Edit value** and **Explore values** stage numbers only, so text choices are
compared and saved through the tool.

`ranked_indices` indexes `results` in objective order and includes only feasible
rows with the requested metric available. `mass_kg` is absent and cannot be ranked
when density is missing. `valid` means compilation and exact terminal-solid
inspection passed; `feasible` also checks caller-entered metric bounds. Program
assertions remain authored rules, not independently qualified requirements.

The scope is terminal feature definitions owned by one program. It does not
multiply assembly occurrences or compute the union of overlapping components.
Replacement still rejects local edits and outside dependants under the existing
parametric-program contract. Stiffness, strength, fatigue, process cost, physical
clearance and manufacturing qualification are not inferred from volume or mass.
Pair clearance is static at the evaluated definition positions, not a
continuous-motion certificate.
AI authors can propose the value sets through this same tool; real-provider design
quality and savings require separate evidence.
