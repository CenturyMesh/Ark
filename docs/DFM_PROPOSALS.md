# Editable DFM proposals (C.03, D.02)

`manufacturing_propose` connects a current versioned recommendation from
`manufacturing_inspect` to explicit editable commands. An agent proposes the
actual design change from the user's requirements and the rule evidence. ARK
evaluates the candidate in isolation and compares exact geometry and all process
rules at the observed revision. No design edit is inferred from a mesh or a
universal material rule.

Load manufacturing onboarding, inspect the target and its editable source, then
supply `expected_revision`, `id`, `rule_id`, `rule_version` and 1..8 commands.
Commands are limited to 64 KiB and existing editable dimensions/parameters,
sketch patches/fillets/chamfers, sheet base/flange/opening/hem/jog/sketch-bend
edits and declared frame-weld edits. `feature.edit` accepts `p` only. Definitions,
active state, material and manufacturing route/process must remain identical.
The selected exact B-rep must actually change; unrelated parameters and no-op
edits reject. Material/density, sampling and tolerance changes are excluded.
Missing valid positive-volume exact geometry, absent/stale recommendations,
process changes and unsupported operations reject explicitly. Manufacturing
process alternatives require a separate explicit intent edit and fresh review.

```json
{
  "expected_revision": 12,
  "id": "Part",
  "rule_id": "DFM.FDM.02",
  "rule_version": 1,
  "commands": [
    {"op":"feature.edit","id":"Part","p":{"width":18}}
  ]
}
```

This example is a proposed dimension edit, not a recommended dimension. It is
useful only when the user's functional requirements permit that change and the
observed exact envelope conflicts with the actual declared build volume.
Expressions stay editable and optional `checks` are enforced before preparation.
The normal CAD evaluation must succeed atomically. Failed candidates retain the
measured `candidate_result` and do not provide a candidate identity.

A valid response includes `recipe`, `changed_feature_ids`, `candidate_id` and
`dfm_comparison`. The comparison retains full `before`/`after` manufacturing
reports, matching process/material inputs, every rule's status transition,
remaining unknown/incompatible rules and their evidence/assumptions. Both
reports refer to the baseline revision; `after.committed` is false.
`target_screen_resolved` means only that this target changed from incompatible
to compatible. A candidate that leaves or worsens a screen remains visible with
that flag false. Other affected components are included to expose regressions.
`manufacturing_qualified`, `cost_improvement_claimed` and
`design_improvement_claimed` remain false.

Review the concrete commands, dimensions, all rule evidence and unresolved
requirements with the user. An optional `snapshot{candidate_id,expected_revision}`
shows the retained candidate; its pixels are display evidence. Acceptance is an
explicit action after the user accepts this concrete edit:

```json
{"expected_revision":12,"candidate_id":"retained candidate identity","accept_dfm":true}
```

Send that payload to `doc_apply`; a team writer can use `team_apply_step` with
its own successful proposal receipt. Missing/false acceptance rejects without
mutation. Team scope/count validation checks the retained recipe and another
role cannot borrow its candidate. Read-only reviewers can inspect/propose and
send the recipe/evidence to the responsible writer; the writer prepares its own
candidate at the observed revision. Agents must not infer user acceptance from
a passing screen.

Candidates expire after 300 seconds or a document/revision change, with the
existing four-candidate/128 MiB cache limits. The retained comparison counts
against cache capacity. Unknown, expired, evicted, changed and reused candidates
reject; they cannot rebase. An accepted candidate reuses evaluated geometry,
commits one revision and one undo entry, and returns `dfm_accepted` plus its
comparison. No `.ark` format version or process ruleset version changes.

C.03 and D.02 remain **Parcial**. [Local verification](verification-dfm-proposals-2026-10-13.json)
covers exact candidate/acceptance contracts and offline team integration.
Functional design acceptance, supplier capability, actual CAM/slicer outputs,
physical trials and production qualification remain separate evidence.
